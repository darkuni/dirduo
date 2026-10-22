#include "core/RemoteBackend.h"

namespace {
RemoteBackend *g_backend = nullptr;
}

RemoteBackend *RemoteBackend::instance()
{
    return g_backend;
}

void RemoteBackend::install(RemoteBackend *backend)
{
    g_backend = backend;
}
