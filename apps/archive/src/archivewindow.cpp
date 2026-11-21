// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivewindow.h"
#include "filemanager.h"
#include "archivepassworddialog.h"
#include <QCheckBox>
#include <QSignalBlocker>
#include <QFormLayout>
#include "filetypes.h"
#include <karyo/ui/paint.h>
#include <QPainter>
#include <QStyledItemDelegate>
#include <karyo/ui/icons.h>
#include <karyo/ui/theme.h>
#include <QApplication>
#include <QDir>
#include <QLocale>
#include <QMap>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace Ui = Karyo::Ui;
using namespace ArchiveBackend;

namespace {
// Same metrics as Files' DetailsDelegate: 30px row, 20px type tile,
// 13px glyph, 13px name, 12.5px metadata, and 8/36px left insets.
class ArchiveDetailsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize result = QStyledItemDelegate::sizeHint(option, index);
        result.setHeight(30);
        return result;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto &t = Ui::Theme::current();
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);
        painter->save();
        const QRect r = option.rect;
        if (index.column() == 0) {
            opt.icon.paint(painter, QRect(r.left() + 8, r.center().y() - 10, 20, 20));
            painter->setFont(Ui::bodyFont(13, option.state & QStyle::State_Selected ? QFont::DemiBold : QFont::Normal));
            painter->setPen(t.text);
            const QRect text(r.left() + 36, r.top(), r.width() - 42, r.height());
            painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                painter->fontMetrics().elidedText(opt.text, Qt::ElideMiddle, text.width()));
        } else {
            painter->setFont(Ui::bodyFont(12.5));
            painter->setPen(t.muted);
            const bool size = index.column() == 1;
            const QRect text = r.adjusted(8, 0, size ? -14 : -8, 0);
            painter->drawText(text, Qt::AlignVCenter | (size ? Qt::AlignRight : Qt::AlignLeft),
                painter->fontMetrics().elidedText(opt.text, Qt::ElideRight, text.width()));
        }
        painter->restore();
    }
};
}

ArchiveWindow::ArchiveWindow(QWidget *parent) : KaryoWindow(parent)
{
    setWindowTitle(tr("Archive Manager"));
    const auto &t = Ui::Theme::current();
    setBodyColor(t.win);
    auto *icon = new QLabel;
    icon->setPixmap(Ui::icon(QStringLiteral("package"), 22, t.accent));
    m_title = new QLabel(tr("Archive Manager"));
    m_title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->setContentsMargins(20, 0, 8, 0);
    titleLayout()->addWidget(icon); titleLayout()->addSpacing(8);
    titleLayout()->addWidget(m_title, 1);

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(16, 0, 16, 12); layout->setSpacing(12);
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    m_open = new Ui::PillButton("folder-open", tr("Open"));
    m_open->setObjectName("openArchive");
    m_new = new Ui::PillButton("plus", tr("New archive"));
    m_new->setObjectName("newArchive");
    m_new->setKind(Ui::PillButton::Kind::Tint);
    m_extract = new Ui::PillButton("folder-output", tr("Extract all"));
    m_extract->setObjectName("extractAll");
    m_extract->setKind(Ui::PillButton::Kind::Primary);
    m_selected = new Ui::PillButton("download", tr("Extract selected"));
    m_selected->setObjectName("extractSelected");
    for (auto *button : {m_open, m_new, m_extract, m_selected}) toolbar->addWidget(button);
    toolbar->addStretch(); layout->addLayout(toolbar);

    auto *navigation = new QHBoxLayout;
    m_up = new Ui::IconButton("arrow-up"); m_up->setHint(tr("Up one folder"));
    m_up->setObjectName("archiveUp");
    m_path = new QLabel(tr("Open an archive to browse its contents"));
    m_path->setTextFormat(Qt::PlainText); m_path->setFont(Ui::bodyFont(13));
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *search = new Ui::SearchField(tr("Filter this folder"));
    search->setFixedWidth(240); search->setFixedHeight(34);
    m_search = search->lineEdit(); m_search->setObjectName("archiveFilter");
    navigation->addWidget(m_up); navigation->addWidget(m_path, 1); navigation->addWidget(search);
    layout->addLayout(navigation);

    m_empty = new QLabel(tr("Your archives, in one place\nOpen an archive or create one from files and folders."));
    m_empty->setAlignment(Qt::AlignCenter); m_empty->setFont(Ui::bodyFont(16));
    m_empty->setMinimumHeight(220);
    layout->addWidget(m_empty, 1);
    m_files = new QTreeWidget;
    m_files->setObjectName("archiveFiles");
    m_files->setHeaderLabels({tr("Name"), tr("Size"), tr("Type"), tr("Modified")});
    m_files->setItemDelegate(new ArchiveDetailsDelegate(m_files));
    m_files->setFont(Ui::bodyFont(13));
    m_files->setRootIsDecorated(false); m_files->setUniformRowHeights(true);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setAllColumnsShowFocus(true); m_files->setFrameShape(QFrame::NoFrame);
    m_files->setAlternatingRowColors(false); m_files->setIndentation(0);
    m_files->header()->setStretchLastSection(false);
    m_files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_files->setColumnWidth(1, 110); m_files->setColumnWidth(2, 120); m_files->setColumnWidth(3, 165);
    m_files->setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; border: 0; }"));
    layout->addWidget(m_files, 1); m_files->hide();

    m_progress = new QProgressBar; m_progress->setTextVisible(false); m_progress->setFixedHeight(5);
    m_progress->hide(); layout->addWidget(m_progress);
    auto *footer = new QHBoxLayout;
    m_status = new QLabel(tr("Ready")); m_status->setTextFormat(Qt::PlainText);
    m_status->setFont(Ui::bodyFont(12)); m_status->setWordWrap(true);
    m_cancelButton = new Ui::PillButton("x", tr("Cancel")); m_cancelButton->hide();
    m_cancelButton->setObjectName("cancelArchiveOperation");
    footer->addWidget(m_status, 1); footer->addWidget(m_cancelButton); layout->addLayout(footer);
    setCentralWidget(body); resizeToFit({1040, 680}); setMinimumSize(780, 450);
    connect(m_open, &QAbstractButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open archive"),
            m_archive.isEmpty() ? QDir::homePath() : QFileInfo(m_archive).absolutePath(),
            tr("Archives (*.zip *.tar *.tar.gz *.tgz *.tar.bz2 *.tar.xz *.tar.zst *.7z *.rar *.gz *.bz2 *.xz *.zst);;All files (*)"));
        if (!path.isEmpty()) openArchive(path);
    });
    connect(m_new, &QAbstractButton::clicked, this, [this] { createArchive(); });
    connect(m_extract, &QAbstractButton::clicked, this, [this] { extract(false); });
    connect(m_selected, &QAbstractButton::clicked, this, [this] { extract(true); });
    connect(m_cancelButton, &QAbstractButton::clicked, this, [this] { if (m_cancel) *m_cancel = true; });
    connect(m_up, &QAbstractButton::clicked, this, [this] {
        m_folder = m_folder.section(QLatin1Char('/'), 0, -2); m_search->clear(); populate();
    });
    connect(m_search, &QLineEdit::textChanged, this, &ArchiveWindow::populate);
    connect(m_files, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { activate(item); });
    connect(m_files, &QTreeWidget::itemSelectionChanged, this, &ArchiveWindow::updateActions);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, m_open, &QAbstractButton::click);
    connect(new QShortcut(QKeySequence::New, this), &QShortcut::activated, m_new, &QAbstractButton::click);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { if (m_cancel) *m_cancel = true; });
    updateActions();
}
ArchiveWindow::~ArchiveWindow() { if (m_cancel) *m_cancel = true; }

void ArchiveWindow::updateActions()
{
    m_open->setEnabled(!m_busy); m_new->setEnabled(!m_busy);
    m_extract->setEnabled(!m_busy && !m_entries.isEmpty());
    m_selected->setEnabled(!m_busy && !m_files->selectedItems().isEmpty());
    m_up->setEnabled(!m_busy && !m_folder.isEmpty());
    m_search->setEnabled(!m_busy && !m_archive.isEmpty()); m_files->setEnabled(!m_busy);
}
void ArchiveWindow::setBusy(bool value)
{
    m_busy = value; m_progress->setVisible(value); m_cancelButton->setVisible(value);
    if (value) m_progress->setRange(0, 0);
    updateActions();
}
void ArchiveWindow::run(const QString &label, const Work &work,
                       const std::function<void(const Result &)> &done, const std::function<void(const QString &)> &retry, const QString &archiveName)
{
    if (m_busy) return;
    setBusy(true); m_status->setText(label);
    auto cancel = m_cancel = std::make_shared<std::atomic_bool>(false);
    QPointer<ArchiveWindow> self(this);
    QThreadPool::globalInstance()->start([self, work, done, retry, cancel, archiveName] {
        QElapsedTimer throttle; throttle.start();
        Progress progress = [self, &throttle](qint64 bytes, qint64 total, const QString &name) {
            if (throttle.elapsed() < 60) return;
            throttle.restart();
            QMetaObject::invokeMethod(qApp, [self, bytes, total, name] {
                if (!self) return;
                if (total > 0) { self->m_progress->setRange(0, 1000); self->m_progress->setValue(int(1000.0 * bytes / total)); }
                self->m_status->setText(tr("%1 · %2").arg(QLocale().formattedDataSize(bytes), name));
            });
        };
        const Result result = work(*cancel, progress);
        QMetaObject::invokeMethod(qApp, [self, result, done, retry, archiveName] {
            if (!self) return;
            self->setBusy(false);
            if (result.cancelled) { self->m_status->setText(tr("Canceled")); return; }
            if (!result.ok()) {
                self->m_status->setText(tr("Operation failed"));
                if (result.passwordRequired && retry) {
                    ArchivePasswordDialog dialog(archiveName, result.passwordRejected, self);
                    if (dialog.exec() == QDialog::Accepted) retry(dialog.password());
                    else self->m_status->setText(tr("Canceled"));
                } else QMessageBox::warning(self, tr("Archive Manager"), result.error);
                return;
            }
            done(result);
        });
    });
}
void ArchiveWindow::openArchive(const QString &path)
{
    openArchiveWithPassword(path, path == m_archive ? m_password : QString());
}
void ArchiveWindow::openArchiveWithPassword(const QString &path, const QString &password)
{
    if (m_busy) return;
    run(tr("Reading archive…"), [path, password](auto &cancel, const auto &) { return ArchiveBackend::list(path, password, cancel); },
        [this, path, password](const Result &result) {
            m_archive = path; m_password = password; m_entries = result.entries; m_folder.clear(); m_search->clear();
            m_title->setText(QFileInfo(path).fileName());
            setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — ") + tr("Archive Manager"));
            populate();
        }, [this, path](const QString &password) { openArchiveWithPassword(path, password); }, QFileInfo(path).fileName());
}
void ArchiveWindow::populate()
{
    m_files->clear();
    struct Row { QString path, name; bool directory; Entry entry; };
    QMap<QString, Row> rows;
    const QString prefix = m_folder.isEmpty() ? QString() : m_folder + QLatin1Char('/');
    qint64 total = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        total += entry.size;
        if (!entry.path.startsWith(prefix)) continue;
        const QString remainder = entry.path.mid(prefix.size());
        if (remainder.isEmpty()) continue;
        const QString name = remainder.section(QLatin1Char('/'), 0, 0);
        if (!name.contains(m_search->text(), Qt::CaseInsensitive)) continue;
        const bool directory = remainder.contains(QLatin1Char('/')) || entry.directory;
        const QString path = prefix + name;
        if (!rows.contains(path) || !remainder.contains(QLatin1Char('/')))
            rows.insert(path, {path, name, directory, entry});
    }
    QList<Row> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), [](const Row &a, const Row &b) {
        return a.directory != b.directory ? a.directory : QString::localeAwareCompare(a.name, b.name) < 0;
    });
    for (const Row &row : sorted) {
        const FileType type = FileType::forName(row.path, row.directory);
        auto *item = new QTreeWidgetItem(m_files);
        item->setText(0, row.name);
        item->setText(1, row.directory ? QString() : formatSize(row.entry.size));
        item->setText(2, row.entry.link ? tr("Link") : row.entry.encrypted ? tr("Encrypted file") : type.label);
        item->setText(3, row.entry.modified.isValid() ? QLocale().toString(row.entry.modified, QStringLiteral("MMM d, yyyy HH:mm")) : QString());
        const qreal dpr = devicePixelRatioF();
        QPixmap tile(QSize(qRound(20 * dpr), qRound(20 * dpr)));
        tile.setDevicePixelRatio(dpr); tile.fill(Qt::transparent);
        QPainter painter(&tile);
        painter.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(painter, QRectF(0, 0, 20, 20), 6, type.tile);
        painter.drawPixmap(QPointF(3.5, 3.5), Ui::icon(type.glyph, 13, type.ink, dpr));
        painter.end();
        item->setIcon(0, QIcon(tile));
        item->setData(0, Qt::UserRole, row.path); item->setData(0, Qt::UserRole + 1, row.directory);
    }
    m_path->setText(m_archive.isEmpty() ? tr("Open an archive to browse its contents") :
        QFileInfo(m_archive).fileName() + QStringLiteral("  /  ") + m_folder);
    m_path->setToolTip(m_archive);
    const bool empty = m_archive.isEmpty() || m_entries.isEmpty();
    m_empty->setVisible(empty); m_files->setVisible(!empty);
    if (!m_archive.isEmpty() && m_entries.isEmpty()) m_empty->setText(tr("This archive is empty."));
    m_status->setText(tr("%1 items · %2 unpacked").arg(m_entries.size()).arg(QLocale().formattedDataSize(total)));
    updateActions();
}
QStringList ArchiveWindow::selectedPaths() const
{
    QStringList result;
    for (auto *item : m_files->selectedItems()) result.append(item->data(0, Qt::UserRole).toString());
    return result;
}
void ArchiveWindow::extract(bool selected)
{
    if (m_busy || m_archive.isEmpty()) return;
    const QStringList paths = selected ? selectedPaths() : QStringList();
    if (selected && paths.isEmpty()) return;
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Extract to folder"), QFileInfo(m_archive).absolutePath());
    if (parent.isEmpty()) return;
    extractTo(parent, paths, m_password);
}
void ArchiveWindow::extractTo(const QString &parent, const QStringList &paths, const QString &password)
{
    const QString archive = m_archive;
    run(tr("Extracting…"), [=](auto &cancel, const auto &progress) { return ArchiveBackend::extract(archive, parent, paths, password, cancel, progress); },
        [this, password](const Result &result) {
            m_password = password;
            m_status->setText(tr("Extracted to %1").arg(result.output));
            Karyo::Apps::openFolder(result.output, this);
        }, [this, parent, paths](const QString &password) { extractTo(parent, paths, password); }, QFileInfo(archive).fileName());
}
void ArchiveWindow::activate(QTreeWidgetItem *item)
{
    if (m_busy || !item) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (item->data(0, Qt::UserRole + 1).toBool()) {
        m_folder = path; m_search->clear(); populate(); return;
    }
    openEntry(path, m_password);
}
void ArchiveWindow::openEntry(const QString &path, const QString &password)
{
    auto temporary = std::make_shared<QTemporaryDir>();
    if (!temporary->isValid()) return;
    const QString archive = m_archive;
    run(tr("Opening file…"), [=](auto &cancel, const auto &progress) { return ArchiveBackend::extract(archive, temporary->path(), {path}, password, cancel, progress); },
        [this, path, temporary, password](const Result &result) {
            m_password = password;
            m_previews.append(temporary);
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(result.output).filePath(path)));
            m_status->setText(tr("Opened %1").arg(path));
        }, [this, path](const QString &password) { openEntry(path, password); }, QFileInfo(archive).fileName());
}
void ArchiveWindow::createArchive(const QStringList &initialSources)
{
    if (m_busy) return;
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("createArchiveDialog")); dialog.setWindowTitle(tr("New archive")); dialog.resize(540, 540);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Choose files and folders to include:")));
    auto *sources = new QListWidget; sources->setObjectName(QStringLiteral("archiveSources")); sources->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(sources, 1);
    auto *actions = new QHBoxLayout;
    auto *addFiles = new QPushButton(tr("Add files")), *addFolder = new QPushButton(tr("Add folder")), *remove = new QPushButton(tr("Remove"));
    actions->addWidget(addFiles); actions->addWidget(addFolder); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
    auto append = [sources](const QStringList &paths) {
        for (const QString &path : paths) if (sources->findItems(path, Qt::MatchExactly).isEmpty()) sources->addItem(path);
    };
    connect(addFiles, &QPushButton::clicked, &dialog, [&, append] { append(QFileDialog::getOpenFileNames(&dialog, tr("Add files"))); });
    connect(addFolder, &QPushButton::clicked, &dialog, [&, append] {
        const QString path = QFileDialog::getExistingDirectory(&dialog, tr("Add folder"));
        if (!path.isEmpty()) append({path});
    });
    connect(remove, &QPushButton::clicked, &dialog, [sources] { qDeleteAll(sources->selectedItems()); });
    append(initialSources);
    auto *format = new QComboBox; format->setObjectName(QStringLiteral("archiveFormat")); format->addItems({QStringLiteral("ZIP (.zip)"), QStringLiteral("TAR.GZ (.tar.gz)"), QStringLiteral("7z (.7z)")});
    auto *formatRow = new QHBoxLayout; formatRow->addWidget(new QLabel(tr("Format"))); formatRow->addWidget(format, 1); layout->addLayout(formatRow);
    auto *compression = new QComboBox; compression->setObjectName(QStringLiteral("archiveCompression"));
    compression->addItem(tr("No compression"), 0); compression->addItem(tr("Fast"), 1);
    compression->addItem(tr("Normal"), 5); compression->addItem(tr("Maximum"), 9); compression->setCurrentIndex(2);
    auto *compressionRow = new QFormLayout; compressionRow->addRow(tr("Compression level"), compression); layout->addLayout(compressionRow);
    auto *password = new QLineEdit; password->setObjectName(QStringLiteral("newArchivePassword")); password->setEchoMode(QLineEdit::Password);
    auto *confirmation = new QLineEdit; confirmation->setObjectName(QStringLiteral("newArchivePasswordConfirm")); confirmation->setEchoMode(QLineEdit::Password);
    auto *passwords = new QFormLayout;
    passwords->addRow(tr("Password (optional)"), password);
    passwords->addRow(tr("Confirm password"), confirmation); layout->addLayout(passwords);
    auto *showPassword = new QCheckBox(tr("Show password")); layout->addWidget(showPassword);
    auto *encryptNames = new QCheckBox(tr("Encrypt file names (7z)"));
    encryptNames->setObjectName(QStringLiteral("encryptArchiveNames")); encryptNames->setChecked(true); layout->addWidget(encryptNames);
    auto *passwordHint = new QLabel; passwordHint->setWordWrap(true); layout->addWidget(passwordHint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Create archive")); buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    bool encryptNamesPreferred = true;
    auto update = [=, &encryptNamesPreferred] {
        const bool supported = format->currentIndex() != 1;
        password->setEnabled(supported); confirmation->setEnabled(supported); showPassword->setEnabled(supported);
        const bool sevenZip = format->currentIndex() == 2;
        const QSignalBlocker blocker(encryptNames);
        encryptNames->setEnabled(sevenZip);
        encryptNames->setChecked(sevenZip && encryptNamesPreferred);
        const bool match = !supported || password->text() == confirmation->text();
        passwordHint->setText(!supported ? tr("Password protection is available for ZIP and 7z archives.")
            : !match ? tr("Passwords do not match.")
            : sevenZip && encryptNamesPreferred && password->text().isEmpty()
                ? tr("Set a password to encrypt file names.") : QString());
        buttons->button(QDialogButtonBox::Save)->setEnabled(sources->count() > 0 && match);
    };
    connect(encryptNames, &QCheckBox::toggled, &dialog, [&, update](bool checked) {
        encryptNamesPreferred = checked; update();
    });
    connect(sources->model(), &QAbstractItemModel::rowsInserted, &dialog, update);
    connect(sources->model(), &QAbstractItemModel::rowsRemoved, &dialog, update);
    connect(format, &QComboBox::currentIndexChanged, &dialog, update);
    connect(password, &QLineEdit::textChanged, &dialog, update);
    connect(confirmation, &QLineEdit::textChanged, &dialog, update);
    connect(showPassword, &QCheckBox::toggled, &dialog, [=](bool show) {
        password->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        confirmation->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    update();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList paths;
    for (int i = 0; i < sources->count(); ++i) paths.append(sources->item(i)->text());
    const QString suffix = QStringList{QStringLiteral(".zip"), QStringLiteral(".tar.gz"), QStringLiteral(".7z")}.at(format->currentIndex());
    const QString filter = QStringLiteral("%1 (*%2)").arg(format->currentText(), suffix);
    QString output = QFileDialog::getSaveFileName(this, tr("Create archive"), QFileInfo(paths.first()).absolutePath()
        + QLatin1Char('/') + QFileInfo(paths.first()).completeBaseName() + suffix, filter);
    if (output.isEmpty()) return;
    if (!output.endsWith(suffix, Qt::CaseInsensitive)) {
        output += suffix;
        if (QFileInfo::exists(output) && QMessageBox::question(this, tr("Replace archive?"),
            tr("%1 already exists. Replace it?").arg(output)) != QMessageBox::Yes) return;
    }
    const auto kind = Format(format->currentIndex());
    const QString secret = kind == Format::TarGz ? QString() : password->text();
    const bool hideNames = encryptNames->isChecked();
    const int level = compression->currentData().toInt();
    run(tr("Creating archive…"), [=](auto &cancel, const auto &progress) { return ArchiveBackend::create(paths, output, kind, cancel, progress, secret, hideNames, level); },
        [this, secret](const Result &result) { openArchiveWithPassword(result.output, secret); });
}
