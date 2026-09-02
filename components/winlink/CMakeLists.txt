# Component registration for winlink, the Winlink radio e-mail client that
# talks to the APRSLink service over ordinary APRS messaging.
#
# message is a public requirement: winlink.h is written against the messaging
# engine's own limits (APRS_MSG_TEXT_STD_MAX bounds every command it accepts),
# so a consumer of this header needs message's include directory too.
#
# main is listed as PRIV_REQUIRES, not REQUIRES: winlink.c uses main's
# app_config.h, json_escape.h, json_store.h, storage.h and str_append.h
# internally, but winlink.h never re-exports any of them, so the dependency does
# not belong on the public REQUIRES list. main in turn lists winlink in its own
# REQUIRES, which makes this a documented circular component dependency (see
# the ESP-IDF build-system guide's "Component Requirements" section): CMake
# resolves it by repeating both component libraries on the linker command line.
# PRIV_REQUIRES main is sufficient on its own to make main's public
# INCLUDE_DIRS visible here, so this component does not add
# "../../main/include" to its own INCLUDE_DIRS.
#
# The cJSON parser is not part of ESP-IDF: it comes from the ESP Component
# Registry as "espressif/cjson" and is listed here because json_store.h, which
# this component includes, parses the mailbox file with it.
idf_component_register(
    SRCS
        "winlink.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        message
    PRIV_REQUIRES
        main
        cjson
        esp_timer
)

# A discarded persistence result is a silent data-loss bug, not a style issue:
# every save entry point declared APRS_MUST_CHECK (see main/include/must_check.h)
# reports a write that may never have reached flash, and the caller that ignores
# it goes on to tell the operator the mailbox is stored. Promote the warning to
# an error for this component's own sources only, so the guarantee holds where
# those call sites live without reaching the IDF or managed component trees.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=unused-result)
