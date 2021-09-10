/* Includes and such */

#include <stddef.h>
#include "php.h"
#include "zend_extensions.h"

/* Handle platform-specific hax */

#ifdef _WIN32
    #define ZEND_EXT_API __declspec(dllexport)
#else
    #define ZEND_EXT_API
#endif

/* Zend extension jazz */

ZEND_EXT_API zend_extension zend_extension_entry = {
    "Tail call optimisation",
    "0.1",
    "Terence C.",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

ZEND_EXTENSION();