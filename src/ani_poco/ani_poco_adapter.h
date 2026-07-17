/*
 * Animator's explicit bridge to Poco's public VM API.
 *
 * The adapter owns all Animator-private state and retained native-POE policy.
 * Callers can configure an already-initialized PocoVmOptions value without
 * importing Animator internals into Poco core or generic module hosts.
 */
#ifndef ANI_POCO_ADAPTER_H
#define ANI_POCO_ADAPTER_H

#include <poco/poco.h>

/* Install every Animator-owned binding category into an otherwise empty VM. */
PocoStatus ani_poco_register_libraries(PocoVm *vm);

/* Write the Animator binding prototypes shown by the Poco-program menu. */
PocoStatus ani_poco_write_library_list(const char *filename);

/* Write the ordered Animator library categories used for baseline audits. */
PocoStatus ani_poco_write_library_inventory(const char *filename);

/* Enable the Animator-only fallback needed by retained native POE modules. */
void ani_poco_configure_legacy_poe(PocoVmOptions *options);

#endif /* ANI_POCO_ADAPTER_H */
