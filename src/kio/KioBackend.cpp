#include "kio/KioBackend.h"

#include "core/Location.h"
#include "kio/RemoteJob.h"

#include <KCoreDirLister>
#include <KFileItem>
#include <KIO/JobUiDelegate>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/SimpleJob>
#include <KProtocolInfo>
#include <KProtocolManager>

#include <QEventLoop>
#include <QMimeDatabase>

namespace {

KioBackend *g_kio = nullptr;

FileItem toFileItem(const KFileItem &k)
{
    FileItem f;
    f.name = k.name();
    f.path = Location::fromUrl(k.url());
    f.isDir = k.isDir();
    f.isSymlink = k.isLink();
    if (f.isSymlink)
        f.symlinkTarget = k.linkDest();
    f.size = f.isDir ? 0 : static_cast<qint64>(k.size());
    f.modified = k.time(KFileItem::ModificationTime);
    f.isHidden = k.isHidden();
    f.isReadable = k.isReadable();
    const mode_t mode = k.permissions();
    QFile::Permissions p;
    if (mode & 0400)
        p |= QFile::ReadOwner | QFile::ReadUser;
    if (mode & 0200)
        p |= QFile::WriteOwner | QFile::WriteUser;
    if (mode & 0100)
        p |= QFile::ExeOwner | QFile::ExeUser;
    if (mode & 0040)
        p |= QFile::ReadGroup;
    if (mode & 0020)
        p |= QFile::WriteGroup;
    if (mode & 0010)
        p |= QFile::ExeGroup;
    if (mode & 0004)
        p |= QFile::ReadOther;
    if (mode & 0002)
        p |= QFile::WriteOther;
    if (mode & 0001)
        p |= QFile::ExeOther;
    f.permissions = p;
    return f;
}

// Runs a quick KIO job to completion with KIO's own prompts available.
bool runJob(KJob *job, QString *error)
{
    if (KioBackend *b = KioBackend::kioInstance())
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, b->window()));
    const bool ok = job->exec();
    if (!ok && error)
        *error = job->errorString();
    return ok;
}

} // namespace

// ---------------------------------------------------------------- lister

KioLister::KioLister(QObject *parent)
    : RemoteLister(parent)
    , m_lister(new KCoreDirLister(this))
{
    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setDelayedMimeTypes(true);
    connect(m_lister, qOverload<>(&KCoreDirLister::completed), this, &KioLister::emitListing);
    connect(m_lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) {
        Q_EMIT failed(m_location, job->errorString());
    });
    // Live updates for protocols that can watch (local, archives).
    connect(m_lister, &KCoreDirLister::itemsAdded, this, [this]() {
        if (!m_lister->isFinished())
            return;
        m_refresh = true;
        emitListing();
    });
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, [this]() {
        m_refresh = true;
        emitListing();
    });
}

void KioLister::open(const QString &location, bool isRefresh)
{
    m_location = location;
    m_refresh = isRefresh;
    m_lister->openUrl(Location::toUrl(location), KCoreDirLister::Reload);
}

bool KioLister::isBusy() const
{
    return !m_lister->isFinished();
}

void KioLister::emitListing()
{
    FileItemList items;
    const KFileItemList k = m_lister->items();
    items.reserve(k.size());
    for (const KFileItem &item : k)
        items << toFileItem(item);
    Q_EMIT listed(m_location, items, m_refresh);
}

// ---------------------------------------------------------------- backend

KioBackend::KioBackend()
{
    g_kio = this;
    RemoteBackend::install(this);
}

KioBackend::~KioBackend()
{
    if (RemoteBackend::instance() == this)
        RemoteBackend::install(nullptr);
    if (g_kio == this)
        g_kio = nullptr;
}

KioBackend *KioBackend::kioInstance()
{
    return g_kio;
}

RemoteLister *KioBackend::createLister(QObject *parent)
{
    return new KioLister(parent);
}

std::shared_ptr<JobBase> KioBackend::createJob(int id, const JobSpec &spec, const QString &queueKey)
{
    return std::make_shared<RemoteJob>(id, spec, queueKey);
}

bool KioBackend::rename(const QString &from, const QString &to, QString *error)
{
    return runJob(KIO::rename(Location::toUrl(from), Location::toUrl(to), KIO::HideProgressInfo), error);
}

bool KioBackend::mkdir(const QString &location, QString *error)
{
    return runJob(KIO::mkdir(Location::toUrl(location)), error);
}

QString KioBackend::archiveLocation(const QString &localFile)
{
    const QString mime = QMimeDatabase().mimeTypeForFile(localFile).name();
    const QString protocol = KProtocolManager::protocolForArchiveMimetype(mime);
    if (protocol.isEmpty() || !KProtocolInfo::isKnownProtocol(protocol))
        return {};
    return Location::fromUrl(QUrl(protocol + QLatin1Char(':') + QUrl::toPercentEncoding(localFile, "/")));
}

void KioBackend::openFile(const QString &location)
{
    auto *job = new KIO::OpenUrlJob(Location::toUrl(location));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, m_window));
    job->start();
}
