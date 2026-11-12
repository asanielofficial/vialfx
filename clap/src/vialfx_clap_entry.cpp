// Plugin factory + the clap_entry export. In a clap-first build, clap-wrapper links the factory
// as a static library and a tiny entry file exports the symbol.
#include "vialfx_clap.h"

#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>
#include <cstring>

namespace {
  uint32_t factoryCount(const clap_plugin_factory*) { return 1; }
  const clap_plugin_descriptor* factoryDesc(const clap_plugin_factory*, uint32_t i) {
    return i == 0 ? vialfx::VialfxClapPlugin::descriptor() : nullptr;
  }
  const clap_plugin* factoryCreate(const clap_plugin_factory*, const clap_host* host, const char* id) {
    if (!clap_version_is_compatible(host->clap_version) ||
        std::strcmp(id, vialfx::VialfxClapPlugin::descriptor()->id) != 0)
      return nullptr;
    return (new vialfx::VialfxClapPlugin(host))->clapPlugin();
  }
  const clap_plugin_factory kFactory = { factoryCount, factoryDesc, factoryCreate };
}

namespace vialfx_entry {
  bool init(const char*) { return true; }
  void deinit() { }
  const void* get_factory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
  }
}
