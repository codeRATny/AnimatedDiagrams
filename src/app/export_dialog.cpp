#include "export_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "ad/export_plan.hpp"
#include "controller.hpp"
#include "qt_render.hpp"

namespace app {

namespace {

constexpr auto kLastDirKey = "export/lastDir";

QString humanSize(qint64 bytes) {
    return bytes < 1024 * 1024 ? QObject::tr("%1 КБ").arg(bytes / 1024)
                               : QObject::tr("%1 МБ").arg(static_cast<double>(bytes) / 1024 / 1024, 0, 'f', 2);
}

QString safeFileName(QString name) {
    static const QRegularExpression bad(QStringLiteral(R"([\\/:*?"<>|\s]+)"));
    name.replace(bad, QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("diagram") : name;
}

}  // namespace

ExportDialog::ExportDialog(Controller& ctl, ad::Rect viewRect, QWidget* parent)
    : QDialog(parent), ctl_(ctl), viewRect_(viewRect) {
    setWindowTitle(tr("Экспорт анимации"));
    setMinimumWidth(460);

    format_ = new QComboBox;
    format_->addItem(tr("GIF — универсально, зацикленно"), formatId(ExportFormat::Gif));
    format_->addItem(tr("WebM (VP9) — лучше качество/размер"), formatId(ExportFormat::WebM));
    format_->addItem(tr("MP4 (H.264) — для презентаций"), formatId(ExportFormat::Mp4));
    format_->addItem(tr("PNG — последовательность кадров"), formatId(ExportFormat::Png));
    if (findFfmpeg().isEmpty()) {
        // без ffmpeg видеоформаты недоступны
        auto* model = qobject_cast<QStandardItemModel*>(format_->model());
        for (int i = 1; i <= 2; ++i) {
            model->item(i)->setEnabled(false);
            model->item(i)->setToolTip(tr("Нужен ffmpeg в PATH"));
        }
    }

    fps_ = new QComboBox;
    for (int f : {8, 12, 15, 20, 24, 30, 45, 60}) fps_->addItem(tr("%1 fps").arg(f), f);
    fps_->setCurrentIndex(fps_->findData(15));

    scale_ = new QComboBox;
    for (double s : {1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0}) scale_->addItem(QStringLiteral("%1×").arg(s), s);

    framing_ = new QComboBox;
    framing_->addItem(tr("По содержимому"), 0);
    framing_->addItem(tr("Как на экране"), 1);

    bgButton_ = new QPushButton;
    auto* darkBtn = new QPushButton(tr("Тёмный"));
    auto* whiteBtn = new QPushButton(tr("Белый"));
    auto* bgRow = new QHBoxLayout;
    bgRow->addWidget(bgButton_, 1);
    bgRow->addWidget(darkBtn);
    bgRow->addWidget(whiteBtn);
    setBackground(background_);
    connect(bgButton_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(background_, this, tr("Фон"));
        if (c.isValid()) setBackground(c);
    });
    connect(darkBtn, &QPushButton::clicked, this, [this] { setBackground(QColor(0x0a, 0x11, 0x1f)); });
    connect(whiteBtn, &QPushButton::clicked, this, [this] { setBackground(Qt::white); });

    loop_ = new QCheckBox(tr("Зациклить (GIF)"));
    loop_->setChecked(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Формат"), format_);
    form->addRow(tr("Частота кадров"), fps_);
    form->addRow(tr("Масштаб"), scale_);
    form->addRow(tr("Кадрирование"), framing_);
    form->addRow(tr("Фон"), bgRow);
    form->addRow(QString(), loop_);

    estimate_ = new QLabel;
    estimate_->setWordWrap(true);
    estimate_->setObjectName("hint");
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    exportButton_ = new QPushButton(tr("Экспортировать"));
    exportButton_->setDefault(true);
    closeButton_ = new QPushButton(tr("Закрыть"));
    auto* buttons = new QDialogButtonBox;
    buttons->addButton(exportButton_, QDialogButtonBox::AcceptRole);
    buttons->addButton(closeButton_, QDialogButtonBox::RejectRole);
    connect(exportButton_, &QPushButton::clicked, this, &ExportDialog::start);
    connect(closeButton_, &QPushButton::clicked, this, &ExportDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(estimate_);
    layout->addWidget(progress_);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    for (QComboBox* c : {format_, fps_, scale_, framing_})
        connect(c, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimate);
    updateEstimate();
}

ExportDialog::~ExportDialog() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
}

void ExportDialog::setBackground(const QColor& c) {
    background_ = c;
    bgButton_->setText(c.name());
    bgButton_->setStyleSheet(QStringLiteral("background: %1; color: %2;")
                                 .arg(c.name(), c.lightness() > 140 ? QStringLiteral("#0b1220") : QStringLiteral("#ffffff")));
}

ExportOptions ExportDialog::options() const {
    ExportOptions o;
    o.format = formatFromId(format_->currentData().toString()).value_or(ExportFormat::Gif);
    o.fps = fps_->currentData().toDouble();
    o.scale = scale_->currentData().toDouble();
    o.framing = framing_->currentData().toInt() == 1 ? Framing::View : Framing::Content;
    o.viewRect = viewRect_;
    o.background = background_;
    o.loop = loop_->isChecked();
    return o;
}

void ExportDialog::updateEstimate() {
    const ExportOptions o = options();
    loop_->setVisible(o.format == ExportFormat::Gif);
    const auto g = planExport(ctl_.model(), o);
    const auto frames = ad::exportFrameTimes(ctl_.duration(), o.fps).size();
    const double mp = static_cast<double>(g.pxW) * g.pxH / 1e6;
    QString text = tr("≈ %1 кадров · %2×%3 px · %4 с").arg(frames).arg(g.pxW).arg(g.pxH).arg(ctl_.duration() / 1000, 0, 'f', 1);
    if (frames > 200 || mp > 6) {
        text += tr("\n⚠ Большой объём: экспорт займёт время, файл будет большим.");
        if (o.format == ExportFormat::Gif) text += tr(" Для длинных/крупных анимаций лучше WebM или MP4.");
    }
    estimate_->setText(text);
}

void ExportDialog::setRunning(bool running) {
    running_ = running;
    exportButton_->setEnabled(!running);
    closeButton_->setText(running ? tr("Прервать") : tr("Закрыть"));
    for (QWidget* w : std::initializer_list<QWidget*>{format_, fps_, scale_, framing_, bgButton_, loop_}) w->setEnabled(!running);
}

void ExportDialog::reject() {
    if (running_) {
        worker_.request_stop();
        status_->setText(tr("Прерывание…"));
        return;
    }
    QDialog::reject();
}

void ExportDialog::start() {
    if (running_) return;
    ExportOptions o = options();
    const QString ext = formatExtension(o.format);
    QSettings settings;
    const QString dir = settings.value(kLastDirKey, QDir::homePath()).toString();
    const QString suggested = dir + QLatin1Char('/') + safeFileName(qs(ctl_.model().meta.name)) + QLatin1Char('.') + ext;
    const QString filter = o.format == ExportFormat::Png ? tr("PNG (*.png)") : tr("%1 (*.%2)").arg(ext.toUpper(), ext);
    QString path = QFileDialog::getSaveFileName(this, tr("Сохранить анимацию"), suggested, filter);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + ext;
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    o.outputPath = path;

    setRunning(true);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    status_->setText(tr("Подготовка…"));
    worker_ = std::jthread([this, model = ctl_.model(), o](std::stop_token stop) {
        auto result = runExport(model, o, stop, [this](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] {
                progress_->setRange(0, total);
                progress_->setValue(done);
                status_->setText(tr("Кадр %1 / %2").arg(done).arg(total));
            }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(this, [this, result = std::move(result)] { finished(result); }, Qt::QueuedConnection);
    });
}

void ExportDialog::finished(const std::expected<ExportResult, QString>& result) {
    if (worker_.joinable()) worker_.join();
    setRunning(false);
    if (result) {
        progress_->setValue(progress_->maximum());
        status_->setText(tr("Готово: %1\n%2 кадров · %3").arg(QDir::toNativeSeparators(result->path)).arg(result->frames)
                             .arg(humanSize(result->bytes)));
    } else {
        progress_->setValue(0);
        status_->setText(result.error());
    }
}

}  // namespace app
