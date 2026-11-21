// SPDX-License-Identifier: GPL-3.0-or-later
#include "detailspane.h"

#include "filetypes.h"

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QApplication>
#include <QCache>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QVBoxLayout>

#include <filesystem>
#include <optional>

namespace Ui = Karyo::Ui;

class TileWidget : public QWidget
{
public:
    TileWidget() { setFixedSize(84, 84); }
    void setType(const FileType &type)
    {
        m_type = type;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        Ui::fillRounded(painter, rect(), 8, m_type.tile.isValid() ? m_type.tile : Ui::Theme::current().card);
        if (!m_type.glyph.isEmpty())
            painter.drawPixmap(22, 22, Ui::icon(m_type.glyph, 40, m_type.ink, devicePixelRatioF()));
    }

private:
    FileType m_type;
};

namespace {

QString permissionsText(QFile::Permissions p, bool dir)
{
    QString s = dir ? QStringLiteral("d") : QStringLiteral("-");
    s += p & QFile::ReadOwner ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteOwner ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeOwner ? QLatin1Char('x') : QLatin1Char('-');
    s += p & QFile::ReadGroup ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteGroup ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeGroup ? QLatin1Char('x') : QLatin1Char('-');
    s += p & QFile::ReadOther ? QLatin1Char('r') : QLatin1Char('-');
    s += p & QFile::WriteOther ? QLatin1Char('w') : QLatin1Char('-');
    s += p & QFile::ExeOther ? QLatin1Char('x') : QLatin1Char('-');
    return s;
}

QString tildePath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.size()) : path;
}

QLabel *mutedLabel(const QString &text, qreal size)
{
    auto *label = new QLabel(text);
    label->setFont(Ui::bodyFont(size));
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    label->setPalette(p);
    return label;
}

constexpr qint64 AutoChecksumLimit = 512LL * 1024 * 1024;

} // namespace

struct DetailsPane::FolderStats {
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 folders = 0;
    int directItems = 0;
    bool complete = true;
    bool cancelled = false;
};

class DetailsPane::FolderSizeCache
{
public:
    std::optional<FolderStats> find(const QString &path)
    {
        QMutexLocker lock(&m_mutex);
        if (const auto *entry = m_entries.object(path); entry && !entry->expires.hasExpired())
            return entry->stats;
        return std::nullopt;
    }

    FolderStats scan(const std::filesystem::path &path)
    {
        namespace fs = std::filesystem;
        std::error_code error;
        fs::directory_iterator it(path, error);
        FolderStats stats;
        while (!error && it != fs::directory_iterator()) {
            if (QThread::currentThread()->isInterruptionRequested())
                return FolderStats{0, 0, 0, 0, false, true};

            if (it->path().filename().native().front() != '.')
                ++stats.directItems;
            std::error_code itemError;
            const auto status = it->symlink_status(itemError);
            if (fs::is_directory(status)) {
                const auto child = scan(it->path());
                if (child.cancelled)
                    return child;
                stats.bytes += child.bytes;
                stats.files += child.files;
                stats.folders += child.folders + 1;
                stats.complete &= child.complete;
            } else if (fs::is_regular_file(status)) {
                const auto bytes = it->file_size(itemError);
                if (!itemError)
                    stats.bytes += qint64(bytes);
                ++stats.files;
            }
            stats.complete &= !itemError;
            it.increment(error);
        }

        stats.cancelled = QThread::currentThread()->isInterruptionRequested();
        stats.complete &= !error && !stats.cancelled;
        if (stats.complete) {
            QMutexLocker lock(&m_mutex);
            m_entries.insert(QFile::decodeName(path.c_str()), new Entry{stats, QDeadlineTimer(60'000)});
        }
        return stats;
    }

private:
    struct Entry {
        FolderStats stats;
        QDeadlineTimer expires;
    };

    QMutex m_mutex;
    QCache<QString, Entry> m_entries{4096};
};

DetailsPane::DetailsPane(QWidget *parent)
    : QWidget(parent)
    , m_folderSizes(std::make_shared<FolderSizeCache>())
    , m_tile(new TileWidget)
    , m_name(new QLabel)
    , m_subtitle(mutedLabel(QString(), 13))
    , m_properties(new QVBoxLayout)
    , m_buttons(new QWidget)
{
    setFixedWidth(300);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 28, 24, 24);
    layout->setSpacing(6);
    layout->addWidget(m_tile);
    layout->addSpacing(14);
    m_name->setFont(Ui::bodyFont(15, QFont::DemiBold));
    m_name->setWordWrap(true);
    m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_name);
    layout->addWidget(m_subtitle);
    layout->addSpacing(18);
    m_properties->setSpacing(10);
    layout->addLayout(m_properties);
    layout->addStretch();

    auto *buttons = new QVBoxLayout(m_buttons);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(10);
    auto *openWith = new Ui::PillButton(QStringLiteral("app-window"), tr("Open with…"));
    openWith->setKind(Ui::PillButton::Kind::Secondary);
    openWith->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *copyPath = new Ui::PillButton(QStringLiteral("copy"), tr("Copy path"));
    copyPath->setKind(Ui::PillButton::Kind::Tint);
    buttons->addWidget(openWith);
    buttons->addWidget(copyPath, 0, Qt::AlignHCenter);
    layout->addWidget(m_buttons);
    connect(openWith, &QAbstractButton::clicked, this, [this] {
        if (!m_path.isEmpty())
            Q_EMIT openWithRequested(m_path);
    });
    connect(copyPath, &QAbstractButton::clicked, this, [this] {
        if (!m_path.isEmpty())
            QApplication::clipboard()->setText(m_path);
    });
}

void DetailsPane::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    Ui::fillRounded(painter, rect(), 8, Ui::Theme::current().pane);
}

void DetailsPane::clearProperties()
{
    while (QLayoutItem *item = m_properties->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

QLabel *DetailsPane::addProperty(const QString &label, const QString &value, bool mono)
{
    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    v->addWidget(mutedLabel(label, 12));
    auto *text = new QLabel(value);
    text->setFont(mono ? Ui::monoFont(13) : Ui::bodyFont(13.5));
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(text);
    m_properties->addWidget(box);
    return text;
}

void DetailsPane::showInfos(const QList<QFileInfo> &infos, const QString &folder)
{
    ++m_generation;
    m_sizeValue = nullptr;
    m_contentsValue = nullptr;
    clearProperties();
    if (infos.size() > 1) {
        qint64 total = 0;
        for (const QFileInfo &info : infos)
            total += info.isDir() ? 0 : info.size();
        FileType type;
        type.glyph = QStringLiteral("layers");
        type.tile = Ui::neutral(200);
        type.ink = Ui::neutral(700);
        m_tile->setType(type);
        m_name->setText(tr("%n items selected", nullptr, int(infos.size())));
        m_subtitle->setText(formatSize(total));
        addProperty(tr("Location"), tildePath(folder));
        m_path.clear();
        m_buttons->hide();
        updateFolderSize();
        return;
    }
    const QFileInfo info = infos.isEmpty() ? QFileInfo(folder) : infos.first();
    m_path = info.absoluteFilePath();
    const FileType type = FileType::of(info);
    m_tile->setType(type);
    m_name->setText(info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName());
    const bool measureFolder = info.isDir() && !infos.isEmpty() && !info.isSymLink();
    if (measureFolder) {
        m_subtitle->setText(tr("Folder"));
    } else if (info.isDir()) {
        const int count = int(QDir(m_path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
        m_subtitle->setText(tr("Folder · %n items", nullptr, count));
    } else {
        m_subtitle->setText(QStringLiteral("%1 · %2").arg(type.label, formatSize(info.size())));
    }
    QString location = tildePath(info.absolutePath());
    if (info.isSymLink())
        location += QStringLiteral(" → ") + tr("link to %1").arg(tildePath(info.symLinkTarget()));
    addProperty(tr("Location"), location);
    addProperty(tr("Modified"), QLocale().toString(info.lastModified(), QStringLiteral("MMM d, yyyy, HH:mm")));
    addProperty(tr("Owner"), QStringLiteral("%1 : %2").arg(info.owner(), info.group()));
    addProperty(tr("Permissions"), permissionsText(info.permissions(), info.isDir()), true);
    m_buttons->setVisible(!info.isDir());
    if (measureFolder) {
        m_sizeValue = addProperty(tr("Size"), tr("Calculating…"));
        m_contentsValue = addProperty(tr("Contains"), tr("Calculating…"));
    }
    updateFolderSize();
    if (info.isDir())
        return;

    // MD5: computed in the background for files up to 512 MB, on request above.
    auto *box = new QWidget;
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    v->addWidget(mutedLabel(tr("Checksum (MD5)"), 12));
    auto *sum = new QLabel;
    sum->setFont(Ui::monoFont(13));
    sum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(sum);
    m_properties->addWidget(box);
    if (info.size() <= AutoChecksumLimit) {
        sum->setText(tr("Calculating…"));
        computeChecksum(m_path, sum);
    } else {
        sum->setText(QStringLiteral("<a href=\"#\">%1</a>").arg(tr("Calculate")));
        sum->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        const QString path = m_path;
        connect(sum, &QLabel::linkActivated, this, [this, sum, path] {
            sum->setTextInteractionFlags(Qt::TextSelectableByMouse);
            sum->setText(tr("Calculating…"));
            computeChecksum(path, sum);
        });
    }

    const auto app = DesktopEntry::defaultFor(type.mimeType);
    addProperty(tr("Opens with"), app ? app->name : tr("No application"));
}

DetailsPane::~DetailsPane()
{
    if (m_folderWorker)
        m_folderWorker->requestInterruption();
}

void DetailsPane::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateFolderSize();
}

void DetailsPane::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_folderWorker)
        m_folderWorker->requestInterruption();
}

void DetailsPane::clearFolderSizes()
{
    m_folderSizes = std::make_shared<FolderSizeCache>();
    if (m_folderWorker)
        m_folderWorker->requestInterruption();
}

void DetailsPane::updateFolderSize()
{
    const QString path = m_sizeValue && isVisible() ? m_path : QString();
    if (m_folderWorker && m_scanningPath != path)
        m_folderWorker->requestInterruption();
    if (path.isEmpty())
        return;
    if (const auto cached = m_folderSizes->find(path)) {
        showFolderSize(*cached);
        return;
    }
    if (m_folderWorker)
        return;

    const auto result = std::make_shared<FolderStats>();
    const auto cache = m_folderSizes;
    m_scanningPath = path;
    auto *worker = QThread::create([path, cache, result] {
        *result = cache->scan(QFile::encodeName(path).constData());
    });
    m_folderWorker = worker;
    connect(worker, &QThread::finished, this, [this, path, result, cache] {
        m_folderWorker = nullptr;
        if (m_sizeValue && isVisible() && m_path == path && m_folderSizes == cache
            && !result->cancelled)
            showFolderSize(*result);
        else
            updateFolderSize();
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start(QThread::LowPriority);
}

void DetailsPane::showFolderSize(const FolderStats &stats)
{
    const QString size = formatSize(stats.bytes);
    m_sizeValue->setText(stats.complete ? size : tr("%1 (partial)").arg(size));
    const QString contents = tr("Files: %1 · Folders: %2").arg(QLocale().toString(stats.files),
                                                           QLocale().toString(stats.folders));
    m_contentsValue->setText(stats.complete ? contents : tr("%1 (partial)").arg(contents));
    if (stats.complete)
        m_subtitle->setText(tr("Folder · %n items", nullptr, stats.directItems));
}

void DetailsPane::computeChecksum(const QString &path, QLabel *target)
{
    QPointer<QLabel> label(target);
    QPointer<DetailsPane> self(this);
    const quint64 generation = m_generation;
    QThread *thread = QThread::create([path, label, generation, self] {
        QFile file(path);
        QString result;
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Md5);
            hash.addData(&file);
            result = QString::fromLatin1(hash.result().toHex());
        }
        // The pane may be gone by the time a large file is hashed.
        QMetaObject::invokeMethod(qApp, [self, label, result, generation] {
            if (!self || !label || generation != self->m_generation)
                return;
            if (result.isEmpty()) {
                label->setText(DetailsPane::tr("Not readable"));
                return;
            }
            label->setText(result.left(4) + QStringLiteral("…") + result.right(4));
            label->setToolTip(result);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}
