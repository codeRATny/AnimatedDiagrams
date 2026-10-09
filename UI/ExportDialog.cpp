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
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "AppContext.hpp"
#include "Controller.hpp"
#include "Export/ExportPlan.hpp"
#include "ExportManager.hpp"
#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kLastDirKey = "export/lastDir";

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
    setWindowTitle(tr("Export Animation"));
    setMinimumWidth(460);
    if (!_background.isValid())
    {
        _background = QColor(0x0a, 0x11, 0x1f);
    }

    _format = new QComboBox;
    _format->addItem(tr("GIF — universal, looping"), FormatId(ExportFormat::Gif));
    _format->addItem(tr("WebM (VP9) — best quality/size"), FormatId(ExportFormat::WebM));
    _format->addItem(tr("MP4 (H.264) — for presentations"), FormatId(ExportFormat::Mp4));
    _format->addItem(tr("PNG — frame sequence"), FormatId(ExportFormat::Png));
    // video formats need a libav encoder for the container
    auto *model = qobject_cast<QStandardItemModel *>(_format->model());
    for (int i = 1; i <= 2; ++i)
    {
        const auto    f       = FormatFromId(_format->itemData(i).toString()).value_or(ExportFormat::Gif);
        const QString encoder = VideoEncoderFor(f);
        model->item(i)->setEnabled(!encoder.isEmpty());
        model->item(i)->setToolTip(encoder.isEmpty() ? tr("No suitable codec in libav") : tr("Codec: %1").arg(encoder));
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

    _quality                       = new QComboBox;
    const QString quality_labels[] = {tr("Smallest size"), tr("Compact"), tr("Balanced"), tr("High"), tr("Maximum")};
    for (int q = 0; q < 5; ++q)
    {
        _quality->addItem(quality_labels[q], q);
    }
    _quality->setCurrentIndex(2);

    _framing = new QComboBox;
    _framing->addItem(tr("Fit to content"), 0);
    _framing->addItem(tr("As on screen"), 1);

    _bg_button      = new QPushButton;
    auto *scene_btn = new QPushButton(tr("Same as scene"));
    auto *white_btn = new QPushButton(tr("White"));
    auto *bg_row    = new QHBoxLayout;
    bg_row->addWidget(_bg_button, 1);
    bg_row->addWidget(scene_btn);
    bg_row->addWidget(white_btn);
    _SetBackground(_background);
    connect(_bg_button, &QPushButton::clicked, this,
            [this]
            {
                const QColor c = QColorDialog::getColor(_background, this, tr("Background"));
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

    _loop = new QCheckBox(tr("Loop (GIF)"));
    _loop->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Format"), _format);
    form->addRow(tr("Frame rate"), _fps);
    form->addRow(tr("Scale"), _scale);
    form->addRow(tr("Video quality"), _quality);
    form->addRow(tr("Framing"), _framing);
    form->addRow(tr("Background"), bg_row);
    form->addRow(QString(), _loop);

    _estimate = new QLabel;
    _estimate->setWordWrap(true);
    _estimate->setObjectName(QStringLiteral("hint"));
    auto *buttons       = new QDialogButtonBox;
    auto *export_button = buttons->addButton(tr("Export in Background"), QDialogButtonBox::AcceptRole);
    export_button->setDefault(true);
    export_button->setObjectName(QStringLiteral("primaryButton"));
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(export_button, &QPushButton::clicked, this, &ExportDialog::_Start);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_estimate);
    layout->addWidget(buttons);

    for (QComboBox *c : {_format, _fps, _scale, _quality, _framing})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &ExportDialog::_UpdateEstimate);
    }
    _UpdateEstimate();
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
    o.quality    = _quality->currentData().toInt();
    return o;
}

void ExportDialog::_UpdateEstimate()
{
    const ExportOptions o = _Options();
    _loop->setVisible(o.format == ExportFormat::Gif);
    _quality->setEnabled(IsVideo(o.format));
    const auto   g      = PlanExport(_ctl.GetModel(), o, _ctl.Reg());
    const auto   frames = ExportFrameTimes(_ctl.Duration(), o.fps).size();
    const double mp     = static_cast<double>(g.px_w) * g.px_h / 1e6;
    QString      text   = tr("≈ %n frame(s) · %1×%2 px · %3 s", nullptr, static_cast<int>(frames))
                       .arg(g.px_w)
                       .arg(g.px_h)
                       .arg(_ctl.Duration() / 1000, 0, 'f', 1);
    if (frames > 200 || mp > 6)
    {
        text += tr("\n⚠ Large output: export will take a while and the file will be big.");
        if (o.format == ExportFormat::Gif)
        {
            text += tr(" For long or large animations, WebM or MP4 works better.");
        }
    }
    _estimate->setText(text);
}

void ExportDialog::_Start()
{
    ExportOptions o   = _Options();
    const QString ext = FormatId(o.format);
    QSettings     settings;
    const QString dir       = settings.value(kLastDirKey, QDir::homePath()).toString();
    const QString suggested = dir + QLatin1Char('/') + SafeFileName(Qs(_ctl.GetModel().meta.name)) + QLatin1Char('.') + ext;
    const QString filter    = tr("%1 (*.%2)").arg(ext.toUpper(), ext);
    QString       path      = QFileDialog::getSaveFileName(this, tr("Save Animation"), suggested, filter);
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

    // the job renders from a snapshot: editing can continue while it runs
    _job_id = _ctl.Context().Exports().Start(Qs(_ctl.GetModel().meta.name), _ctl.GetModel(), _ctl.Reg(), o);
    accept();
}

} // namespace ad::ui
