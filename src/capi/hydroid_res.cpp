#include "hydroid_res.h"

#include "hydroid/res/Version.h"

extern "C" const char* hydroid_res_version(void) { return hydroid::res::version(); }
