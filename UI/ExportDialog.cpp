#include "ExportDialog.hpp"

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

#include "Controller.hpp"
#include "Export/ExportPlan.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kLastDirKey = "export/lastDir";

QString HumanSize(qint64 bytes)
{
    return bytes < 1024 * 1024 ? QObject::tr("%1 КБ").arg(bytes / 1024)
                               : QObject::tr("%1 МБ").arg(static_cast<double>(bytes) / 1024 / 1024, 0, 'f', 2);
}

QString SafeFileName(QString name)
{
    static const QRegularExpression kBad(QStringLiteral(R"([\\/:*?"<>|\s]+)"));
    name.replace(kBad, QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("diagram") : name;
}

} // namespace

ExportDialog::ExportDialog(Controller &ctl, Rect view_rect, QWidget *parent)
    : QDialog(parent), _ctl(ctl), _view_rect(view_rect), _background(Qs(ctl.GetModel().scene.background))
{
    setWindowTitle(tr("Экспорт анимации"));
    setMinimumWidth(460);
    if (!_background.isValid())
    {
        _background = QColor(0x0a, 0x11, 0x1f);
    }

    _format = new QComboBox;
    _format->addItem(tr("GIF — универсально, зацикленно"), FormatId(ExportFormat::Gif));
    _format->addItem(tr("WebM (VP9) — лучше качество/размер"), FormatId(ExportFormat::WebM));
    _format->addItem(tr("MP4 (H.264) — для презентаций"), FormatId(ExportFormat::Mp4));
    _format->addItem(tr("PNG — последовательность кадров"), FormatId(ExportFormat::Png));
    if (FindFfmpeg().isEmpty())
    {
        // video formats need ffmpeg
        auto *model = qobject_cast<QStandardItemModel *>(_format->model());
        for (int i = 1; i <= 2; ++i)
        {
            model->item(i)->setEnabled(false);
            model->item(i)->setToolTip(tr("Нужен ffmpeg в PATH"));
        }
    }

    _fps = new QComboBox;
    for (const int f : {8, 12, 15, 20, 24, 30, 45, 60})
    {
        _fps->addItem(tr("%1 fps").arg(f), f);
    }
    _fps->setCurrentIndex(_fps->findData(15));

    _scale = new QComboBox;
    for (const double s : {1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0})
    {
        _scale->addItem(QStringLiteral("%1×").arg(s), s);
    }

    _framing = new QComboBox;
    _framing->addItem(tr("По содержимому"), 0);
    _framing->addItem(tr("Как на экране"), 1);

    _bg_button      = new QPushButton;
    auto *scene_btn = new QPushButton(tr("Как у сцены"));
    auto *white_btn = new QPushButton(tr("Белый"));
    auto *bg_row    = new QHBoxLayout;
    bg_row->addWidget(_bg_button, 1);
    bg_row->addWidget(scene_btn);
    bg_row->addWidget(white_btn);
    _SetBackground(_background);
    connect(_bg_button, &QPushButton::clicked, this,
            [this]
            {
                const QColor c = QColorDialog::getColor(_background, this, tr("Фон"));
                if (c.isValid())
                {
                    _SetBackground(c);
                }
            });
    connect(scene_btn, &QPushButton::clicked, this,
            [this]
            {
                _SetBackground(QColor(Qs(_ctl.GetModel().scene.background)));
            });
    connect(white_btn, &QPushButton::clicked, this,
            [this]
            {
                _SetBackground(Qt::white);
            });

    _loop = new QCheckBox(tr("Зациклить (GIF)"));
    _loop->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Формат"), _format);
    form->addRow(tr("Частота кадров"), _fps);
    form->addRow(tr("Масштаб"), _scale);
    form->addRow(tr("Кадрирование"), _framing);
    form->addRow(tr("Фон"), bg_row);
    form->addRow(QString(), _loop);

    _estimate = new QLabel;
    _estimate->setWordWrap(true);
    _estimate->setObjectName(QStringLiteral("hint"));
    _progress = new QProgressBar;
    _progress->setRange(0, 1);
    _progress->setValue(0);
    _progress->setTextVisible(false);
    _status = new QLabel;
    _status->setWordWrap(true);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    _export_button = new QPushButton(tr("Экспортировать"));
    _export_button->setDefault(true);
    _close_button = new QPushButton(tr("Закрыть"));
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(_export_button, QDialogButtonBox::AcceptRole);
    buttons->addButton(_close_button, QDialogButtonBox::RejectRole);
    connect(_export_button, &QPushButton::clicked, this, &ExportDialog::_Start);
    connect(_close_button, &QPushButton::clicked, this, &ExportDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_estimate);
    layout->addWidget(_progress);
    layout->addWidget(_status);
    layout->addWidget(buttons);

    for (QComboBox *c : {_format, _fps, _scale, _framing})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &ExportDialog::_UpdateEstimate);
    }
    _UpdateEstimate();
}

ExportDialog::~ExportDialog()
{
    _worker.request_stop();
    if (_worker.joinable())
    {
        _worker.join();
    }
}

void ExportDialog::_SetBackground(const QColor &c)
{
    _background = c.isValid() ? c : QColor(0x0a, 0x11, 0x1f);
    _bg_button->setText(_background.name());
    _bg_button->setStyleSheet(
        QStringLiteral("background: %1; color: %2;")
            .arg(_background.name(), _background.lightness() > 140 ? QStringLiteral("#0b1220") : QStringLiteral("#ffffff")));
}

ExportOptions ExportDialog::_Options() const
{
    ExportOptions o;
    o.format     = FormatFromId(_format->currentData().toString()).value_or(ExportFormat::Gif);
    o.fps        = _fps->currentData().toDouble();
    o.scale      = _scale->currentData().toDouble();
    o.framing    = _framing->currentData().toInt() == 1 ? Framing::View : Framing::Content;
    o.view_rect  = _view_rect;
    o.background = _background;
    o.loop       = _loop->isChecked();
    return o;
}

void ExportDialog::_UpdateEstimate()
{
    const ExportOptions o = _Options();
    _loop->setVisible(o.format == ExportFormat::Gif);
    const auto   g      = PlanExport(_ctl.GetModel(), o, _ctl.Reg());
    const auto   frames = ExportFrameTimes(_ctl.Duration(), o.fps).size();
    const double mp     = static_cast<double>(g.px_w) * g.px_h / 1e6;
    QString      text   = tr("≈ %1 кадров · %2×%3 px · %4 с").arg(frames).arg(g.px_w).arg(g.px_h).arg(_ctl.Duration() / 1000, 0, 'f', 1);
    if (frames > 200 || mp > 6)
    {
        text += tr("\n⚠ Большой объём: экспорт займёт время, файл будет большим.");
        if (o.format == ExportFormat::Gif)
        {
            text += tr(" Для длинных/крупных анимаций лучше WebM или MP4.");
        }
    }
    _estimate->setText(text);
}

void ExportDialog::_SetRunning(bool running)
{
    _running = running;
    _export_button->setEnabled(!running);
    _close_button->setText(running ? tr("Прервать") : tr("Закрыть"));
    for (QWidget *w : std::initializer_list<QWidget *>{_format, _fps, _scale, _framing, _bg_button, _loop})
    {
        w->setEnabled(!running);
    }
}

void ExportDialog::reject()
{
    if (_running)
    {
        _worker.request_stop();
        _status->setText(tr("Прерывание…"));
        return;
    }
    QDialog::reject();
}

void ExportDialog::_Start()
{
    if (_running)
    {
        return;
    }
    ExportOptions o   = _Options();
    const QString ext = FormatId(o.format);
    QSettings     settings;
    const QString dir       = settings.value(kLastDirKey, QDir::homePath()).toString();
    const QString suggested = dir + QLatin1Char('/') + SafeFileName(Qs(_ctl.GetModel().meta.name)) + QLatin1Char('.') + ext;
    const QString filter    = tr("%1 (*.%2)").arg(ext.toUpper(), ext);
    QString       path      = QFileDialog::getSaveFileName(this, tr("Сохранить анимацию"), suggested, filter);
    if (path.isEmpty())
    {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty())
    {
        path += QLatin1Char('.') + ext;
    }
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    o.output_path = path;

    _SetRunning(true);
    _progress->setRange(0, 1);
    _progress->setValue(0);
    _status->setText(tr("Подготовка…"));
    // the worker gets copies: the document may change while exporting
    _worker = std::jthread(
        [this, model = _ctl.GetModel(), reg = _ctl.Reg(), o](const std::stop_token &stop)
        {
            auto result = RunExport(model, o, reg, stop,
                                    [this](int done, int total)
                                    {
                                        QMetaObject::invokeMethod(
                                            this,
                                            [this, done, total]
                                            {
                                                _progress->setRange(0, total);
                                                _progress->setValue(done);
                                                _status->setText(tr("Кадр %1 / %2").arg(done).arg(total));
                                            },
                                            Qt::QueuedConnection);
                                    });
            QMetaObject::invokeMethod(
                this,
                [this, result = std::move(result)]
                {
                    _Finished(result);
                },
                Qt::QueuedConnection);
        });
}

void ExportDialog::_Finished(const std::expected<ExportResult, QString> &result)
{
    if (_worker.joinable())
    {
        _worker.join();
    }
    _SetRunning(false);
    if (result.has_value())
    {
        _progress->setValue(_progress->maximum());
        _status->setText(
            tr("Готово: %1\n%2 кадров · %3").arg(QDir::toNativeSeparators(result->path)).arg(result->frames).arg(HumanSize(result->bytes)));
    }
    else
    {
        _progress->setValue(0);
        _status->setText(result.error());
    }
}

} // namespace ad::ui
