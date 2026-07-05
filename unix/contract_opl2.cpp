#include "../adplug/dosbox/dosbox.h"
#include "../adplug/dosbox_opls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace DBOPL2
{
#undef OPLTYPE_IS_OPL3
#include "../adplug/dosbox/opl.cpp.h"
}
