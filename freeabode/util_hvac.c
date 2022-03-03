#include "config.h"

#include <freeabode/freeabode.pb-c.h>

#include "util_hvac.h"

const char *hvacwire_name(const PbHVACWires wire)
{
	switch (wire) {
		case PB_HVACWIRES__Y1: return "compressor (Y1)";
		case PB_HVACWIRES__Y2: return "cool 2 (Y2)";
		case PB_HVACWIRES__W1: return "heat (W1)";
		case PB_HVACWIRES__W2: return "heat 2 (W2)";
		case PB_HVACWIRES__G : return "fan (G)";
		case PB_HVACWIRES__OB: return "reversing (OB)";
		case PB_HVACWIRES__Star: return "aux";
	}
	return "UNKNOWN";
}
