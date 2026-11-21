// SPDX-License-Identifier: GPL-3.0-or-later
// System › Storage, after Windows: the disks, what fills the home folder
// (measured in a background thread, as du counts it), what can be cleaned
// up (conda's and pip's package caches, thumbnails, apt's downloads, the
// Trash) and the largest files, which on a lab machine are BAM and FASTQ.
#include "pages.h"
#include "storageusage.h"
#include "filemanager.h"
#include "fileops.h"
#include "widgets.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>
#include <memory>


namespace Ui = Karyo::Ui;

namespace {

using StorageUsage::Part;
using StorageUsage::Scan;
QStringList condaRoots() { return StorageUsage::condaRoots(QDir::homePath()); }

// The home folder's parts side by side in one bar.
class PartsBar : public QWidget
{
public:
    explicit PartsBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(14);
    }
    void setParts(const QList<QPair<qint64, QColor>> &parts, qint64 total)
    {
        m_parts = parts;
        m_total = total;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        p.setClipPath(clip);
        p.fillRect(rect(), t.press);
        qreal x = 0;
        for (const auto &part : std::as_const(m_parts)) {
            if (m_total <= 0)
                break;
            const qreal w = width() * qreal(part.first) / m_total;
            p.fillRect(QRectF(x, 0, w + 0.5, height()), part.second);
            x += w;
        }
    }

private:
    QList<QPair<qint64, QColor>> m_parts;
    qint64 m_total = 0;
};

// A color dot before a row's title, matching its part of the bar.
class Dot : public QWidget
{
public:
    Dot(const QColor &color, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_color(color)
    {
        setFixedSize(12, 12);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(rect());
    }

private:
    QColor m_color;
};

// GB as Files and Windows write it (1024-based).
QString dataSize(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat); }

void showInFiles(const QString &path)
{
    if (QFileInfo(path).isDir()) Karyo::Apps::openFolder(path);
    else Karyo::Apps::showInFiles(path);
}
QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

class StoragePage : public SettingsPage
{
    Q_OBJECT
public:
    explicit StoragePage(QWidget *parent)
        : SettingsPage(tr("Storage"), parent)
        , m_cancel(std::make_shared<std::atomic_bool>(false))
    {
        addKeywords({tr("Disk"), tr("Space"), tr("Clean up"), tr("Large files"), QStringLiteral("conda")});
        addVolumes();
        auto *header = new QHBoxLayout;
        header->addWidget(sectionHeading(tr("Home folder")));
        header->addStretch(1);
        m_again = new Ui::PillButton(QStringLiteral("refresh-cw"), tr("Measure again"), this);
        m_again->setKind(Ui::PillButton::Kind::Secondary);
        connect(m_again, &QAbstractButton::clicked, this, &StoragePage::start);
        header->addWidget(m_again);
        column()->addLayout(header);
        m_results = new QVBoxLayout;
        m_results->setSpacing(10);
        column()->addLayout(m_results);
        start();
    }

    ~StoragePage() override { m_cancel->store(true); }

private:
    void addVolumes()
    {
        column()->addWidget(sectionHeading(tr("Disks")));
        // One row per disk: bind mounts show the same device again.
        QList<QStorageInfo> volumes;
        for (const QStorageInfo &info : QStorageInfo::mountedVolumes()) {
            auto same = std::find_if(volumes.begin(), volumes.end(),
                                     [&info](const QStorageInfo &v) { return v.device() == info.device(); });
            if (same == volumes.end())
                volumes.append(info);
            else if (info.rootPath().size() < same->rootPath().size())
                *same = info;
        }
        for (const QStorageInfo &info : std::as_const(volumes)) {
            // Real disks only: no tmpfs, loops, snaps or the VM's shared folder.
            if (!info.isValid() || !info.isReady() || !info.device().startsWith("/dev/")
                || info.device().startsWith("/dev/loop") || info.bytesTotal() <= 0)
                continue;
            const QString root = info.rootPath();
            if (root.startsWith(QLatin1String("/boot")) || root.startsWith(QLatin1String("/snap")))
                continue;
            const qint64 total = info.bytesTotal();
            const qint64 used = total - info.bytesAvailable();
            QString name = root == QLatin1String("/") ? tr("System (/)") : info.displayName();
            if (root == QDir::homePath())
                name = tr("Home");
            auto *row = new SettingRow(QStringLiteral("hard-drive"), name,
                                       tr("%1 used of %2 · %3 free")
                                           .arg(dataSize(used), dataSize(total), dataSize(info.bytesAvailable())),
                                       this);
            auto *bar = new PartsBar(row);
            bar->setFixedWidth(180);
            bar->setParts({{used, used > total * 0.9 ? QColor(0xc6, 0x71, 0x39) : Ui::Theme::current().accent}}, total);
            row->setControl(bar);
            column()->addWidget(row);
        }
    }

    void start()
    {
        m_cancel->store(true);
        m_cancel = std::make_shared<std::atomic_bool>(false);
        clearResults();
        m_results->addWidget(note(tr("Measuring your home folder…")));
        m_again->setEnabled(false);

        QList<Part> parts;
        auto add = [&parts](const QString &key, const QString &glyph, QStandardPaths::StandardLocation location) {
            const QString path = QStandardPaths::writableLocation(location);
            if (!path.isEmpty() && path != QDir::homePath() && QFileInfo(path).isDir())
                parts.append({key, glyph, path});
        };
        add(QStringLiteral("documents"), QStringLiteral("file-text"), QStandardPaths::DocumentsLocation);
        add(QStringLiteral("downloads"), QStringLiteral("download"), QStandardPaths::DownloadLocation);
        add(QStringLiteral("desktop"), QStringLiteral("monitor"), QStandardPaths::DesktopLocation);
        add(QStringLiteral("pictures"), QStringLiteral("image"), QStandardPaths::PicturesLocation);
        add(QStringLiteral("music"), QStringLiteral("music"), QStandardPaths::MusicLocation);
        add(QStringLiteral("videos"), QStringLiteral("film"), QStandardPaths::MoviesLocation);
        const QStringList conda = condaRoots();
        if (!conda.isEmpty())
            parts.append({QStringLiteral("conda"), QStringLiteral("boxes"), QString()});
        parts.append({QStringLiteral("cache"), QStringLiteral("layers"), QDir::homePath() + QStringLiteral("/.cache")});
        parts.append({QStringLiteral("trash"), QStringLiteral("trash-2"),
                      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash")});
        parts.append({QStringLiteral("other"), QStringLiteral("folder"), QString()});

        const auto cancel = m_cancel;
        QPointer<StoragePage> self(this);
        QThread *thread = QThread::create([parts, cancel, self] {
            const Scan scan = StorageUsage::measure(QDir::homePath(), parts, *cancel);
            if (cancel->load())
                return;
            QMetaObject::invokeMethod(
                qApp,
                [self, scan, cancel] {
                    if (self && !cancel->load())
                        self->showScan(scan);
                },
                Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start(QThread::LowPriority);
    }

    void clearResults()
    {
        while (QLayoutItem *item = m_results->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
    }

    QString title(const QString &key) const
    {
        static const QHash<QString, const char *> titles = {
            {QStringLiteral("documents"), QT_TR_NOOP("Documents")}, {QStringLiteral("downloads"), QT_TR_NOOP("Downloads")},
            {QStringLiteral("desktop"), QT_TR_NOOP("Desktop")},     {QStringLiteral("pictures"), QT_TR_NOOP("Pictures")},
            {QStringLiteral("music"), QT_TR_NOOP("Music")},         {QStringLiteral("videos"), QT_TR_NOOP("Videos")},
            {QStringLiteral("conda"), QT_TR_NOOP("Conda environments")},
            {QStringLiteral("cache"), QT_TR_NOOP("Caches")},        {QStringLiteral("trash"), QT_TR_NOOP("Trash")},
            {QStringLiteral("other"), QT_TR_NOOP("Other")},
        };
        return tr(titles.value(key));
    }

    void showScan(const Scan &scan)
    {
        clearResults();
        m_again->setEnabled(true);
        if (!scan.errors.isEmpty()) {
            auto *warning = note(tr("Some paths could not be read. Totals may be incomplete:\n%1").arg(scan.errors.join(QLatin1Char('\n'))));
            warning->setTextFormat(Qt::PlainText);
            m_results->addWidget(warning);
        }
        const QList<QColor> colors = {QColor(0x6f, 0x8f, 0x5e), QColor(0xc6, 0x71, 0x39), QColor(0xc9, 0xa2, 0x4a),
                                      QColor(0x5f, 0x8a, 0x9e), QColor(0x9a, 0x6f, 0x9e), QColor(0xb5, 0x5a, 0x5a),
                                      QColor(0x4f, 0x9e, 0x8a), QColor(0x8a, 0x7a, 0x62), QColor(0xa3, 0x8f, 0x6f),
                                      QColor(0x80, 0x80, 0x80)};

        // The bar, then a row per part, biggest first; small ones stay out.
        QList<Part> parts = scan.parts;
        std::sort(parts.begin(), parts.end(), [](const Part &a, const Part &b) {
            if ((a.key == QLatin1String("other")) != (b.key == QLatin1String("other")))
                return b.key == QLatin1String("other");
            return a.bytes > b.bytes;
        });
        auto *summary = new SettingRow(QStringLiteral("house"), tr("Home"),
                                       tr("%1 in your files").arg(dataSize(scan.total)), this);
        auto *bar = new PartsBar(summary);
        bar->setFixedWidth(260);
        summary->setControl(bar);
        m_results->addWidget(summary);
        QList<QPair<qint64, QColor>> segments;
        for (int i = 0; i < parts.size(); ++i) {
            const Part &part = parts.at(i);
            if (part.bytes < 1024 * 1024 && part.key != QLatin1String("other"))
                continue;
            const QColor color = colors.at(i % colors.size());
            segments.append({part.bytes, color});
            auto *row = new SettingRow(part.glyph, title(part.key), QString(), this);
            row->setLink(dataSize(part.bytes));
            QString target = part.path;
            if (part.key == QLatin1String("trash"))
                target += QStringLiteral("/files");
            else if (part.key == QLatin1String("conda") || part.key == QLatin1String("other"))
                target = part.key == QLatin1String("conda") ? condaRoots().value(0) : QDir::homePath();
            connect(row, &SettingRow::clicked, this, [target] { showInFiles(target); });
            auto *dot = new Dot(color, row);
            dot->move(14, 14);  // over the glyph circle's corner
            m_results->addWidget(row);
        }
        bar->setParts(segments, scan.total);

        // What can go.
        m_results->addWidget(sectionHeading(tr("Clean up")));
        bool any = false;
        auto cleanup = [this, &any](const QString &glyph, const QString &title, const QString &description,
                                    qint64 bytes, const QString &button, std::function<void()> action) {
            if (bytes < 1024 * 1024)
                return;
            any = true;
            auto *row = new SettingRow(glyph, title, description + QStringLiteral(" · ") + dataSize(bytes), this);
            auto *pill = new Ui::PillButton(QString(), button, row);
            pill->setKind(Ui::PillButton::Kind::Secondary);
            connect(pill, &QAbstractButton::clicked, this, action);
            row->setControl(pill);
            m_results->addWidget(row);
        };
        const QString condaRoot = condaRoots().value(0);
        const QString condaTool = QDir(condaRoot).filePath(QStringLiteral("bin/conda"));
        if (QFileInfo(condaTool).isExecutable()) {
            cleanup(QStringLiteral("boxes"), tr("Conda package cache"),
                    tr("Downloaded packages; environments keep working"), scan.extras.value(QStringLiteral("condapkgs")),
                    tr("Clean"), [condaTool] {
                        runInTerminal(shellQuote(condaTool) + QStringLiteral(" clean --all --yes"));
                    });
        }
        cleanup(QStringLiteral("package"), tr("pip cache"), tr("Downloaded Python wheels"),
                scan.extras.value(QStringLiteral("pip")), tr("Clean"),
                [this] { cleanCache(QStringLiteral("pip")); });
        cleanup(QStringLiteral("image"), tr("Thumbnails"), tr("Previews Files and Photos make again as needed"),
                scan.extras.value(QStringLiteral("thumbnails")), tr("Clean"), [this] {
                    cleanCache(QStringLiteral("thumbnails"));
                });
        cleanup(QStringLiteral("download"), tr("Downloaded Debian packages"),
                tr("apt's copies of installed packages"), scan.extras.value(QStringLiteral("apt")), tr("Clean"),
                [] { runInTerminal(QStringLiteral("sudo apt-get clean && echo Done")); });
        for (const Part &part : std::as_const(parts)) {
            if (part.key == QLatin1String("trash"))
                cleanup(QStringLiteral("trash-2"), tr("Trash"), tr("Files you deleted"), part.bytes, tr("Open"),
                        [path = part.path] { showInFiles(path + QStringLiteral("/files")); });
        }
        if (!any)
            m_results->addWidget(note(tr("Nothing to clean up.")));

        // The biggest files, where a lab's space goes.
        if (!scan.largest.isEmpty()) {
            m_results->addWidget(sectionHeading(tr("Large files")));
            const QString home = QDir::homePath();
            for (const auto &file : scan.largest) {
                const QFileInfo info(file.second);
                QString folder = QDir::toNativeSeparators(info.absolutePath());
                if (folder.startsWith(home))
                    folder.replace(0, home.size(), QStringLiteral("~"));
                auto *row = new SettingRow(QStringLiteral("file-text"), info.fileName(),
                                           folder + QStringLiteral(" · ")
                                               + QLocale().toString(info.lastModified().date(), QLocale::ShortFormat),
                                           this);
                row->setLink(dataSize(file.first));
                connect(row, &SettingRow::clicked, this, [path = file.second] { showInFiles(path); });
                m_results->addWidget(row);
            }
        }

        // Installed apps, outside home.
        m_results->addWidget(sectionHeading(tr("System")));
        auto *apps = new SettingRow(QStringLiteral("package"), tr("Installed apps"),
                                    tr("Debian packages on this computer"), this);
        apps->setValue(dataSize(scan.extras.value(QStringLiteral("apps"))));
        m_results->addWidget(apps);
    }

    void cleanCache(const QString &name)
    {
        const QString cache = QDir::homePath() + QStringLiteral("/.cache");
        const QString path = QDir(cache).filePath(name);
        // Do not follow a redirected cache root into user documents or another tree.
        if (QFileInfo(cache).isSymLink() || QFileInfo(path).isSymLink()) {
            QMessageBox::warning(this, tr("Clean up"), tr("The cache path is a symbolic link. Open it in Files to review it first."));
            return;
        }
        if (QMessageBox::question(this, tr("Clean up"), tr("Delete cached files in %1?").arg(path)) != QMessageBox::Yes) return;
        auto *job = FileJob::start(FileJob::Kind::Delete, {path});
        connect(job, &FileJob::finished, this, [this](const QStringList &errors) {
            if (!errors.isEmpty()) QMessageBox::warning(this, tr("Clean up"), errors.join(QLatin1Char('\n')));
            start();
        });
    }

    std::shared_ptr<std::atomic_bool> m_cancel;
    Ui::PillButton *m_again;
    QVBoxLayout *m_results;
};

} // namespace

SettingsPage *createStoragePage(QWidget *parent) { return new StoragePage(parent); }

#include "storage.moc"
