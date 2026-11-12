#include <clap/entry.h>
#include <clap/version.h>

namespace vialfx_entry {
  bool init(const char*);
  void deinit();
  const void* get_factory(const char*);
}

extern "C" {
  CLAP_EXPORT const clap_plugin_entry clap_entry = {
    CLAP_VERSION, vialfx_entry::init, vialfx_entry::deinit, vialfx_entry::get_factory
  };
}
