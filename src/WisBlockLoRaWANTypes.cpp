/**
 * @file WisBlockLoRaWANTypes.cpp
 * @brief Conversion helpers between the two region enumerations used across this library:
 * WisBlockRegion (mirrors LBM's smtc_modem_region_t, used internally and by the persisted
 * config/most of the public API) and WisBlockRUI3Band (RUI3's AT+BAND numbering, used by
 * WisBlockLoRaWAN::setRegion()/getRegion() and AT+BAND). See WisBlockRUI3Band's doc comment in
 * WisBlockLoRaWANTypes.h for why these two numberings exist and don't match each other.
 *
 * Previously this switch/inverse-switch pair only existed as file-local (anonymous-namespace)
 * helpers inside WisBlockLoRaAT.cpp for AT+BAND's own use; they now live here so
 * WisBlockLoRaWAN::setRegion()/getRegion() (the C++ API, not just the AT layer) can share the
 * exact same conversion instead of the two ever risking drifting apart.
 */
#include "WisBlockLoRaWANTypes.h"

bool wisblockRUI3BandToRegion(WisBlockRUI3Band band, WisBlockRegion &outRegion)
{
	switch (band)
	{
	case WISBLOCK_RUI3_BAND_CN470:
		outRegion = WISBLOCK_REGION_CN470;
		return true;
	case WISBLOCK_RUI3_BAND_RU864:
		outRegion = WISBLOCK_REGION_RU864;
		return true;
	case WISBLOCK_RUI3_BAND_IN865:
		outRegion = WISBLOCK_REGION_IN865;
		return true;
	case WISBLOCK_RUI3_BAND_EU868:
		outRegion = WISBLOCK_REGION_EU868;
		return true;
	case WISBLOCK_RUI3_BAND_US915:
		outRegion = WISBLOCK_REGION_US915;
		return true;
	case WISBLOCK_RUI3_BAND_AU915:
		outRegion = WISBLOCK_REGION_AU915;
		return true;
	case WISBLOCK_RUI3_BAND_KR920:
		outRegion = WISBLOCK_REGION_KR920;
		return true;
	case WISBLOCK_RUI3_BAND_AS923_1:
		outRegion = WISBLOCK_REGION_AS923_1;
		return true;
	case WISBLOCK_RUI3_BAND_AS923_2:
		outRegion = WISBLOCK_REGION_AS923_2;
		return true;
	case WISBLOCK_RUI3_BAND_AS923_3:
		outRegion = WISBLOCK_REGION_AS923_3;
		return true;
	case WISBLOCK_RUI3_BAND_AS923_4:
		outRegion = WISBLOCK_REGION_AS923_4;
		return true;
	default: // WISBLOCK_RUI3_BAND_EU433 (0), WISBLOCK_RUI3_BAND_LA915 (12),
			 // WISBLOCK_RUI3_BAND_UNKNOWN, and anything else out of range
		return false;
	}
}

WisBlockRUI3Band wisblockRegionToRUI3Band(WisBlockRegion region)
{
	switch (region)
	{
	case WISBLOCK_REGION_EU868:
		return WISBLOCK_RUI3_BAND_EU868;
	case WISBLOCK_REGION_US915:
		return WISBLOCK_RUI3_BAND_US915;
	case WISBLOCK_REGION_AU915:
		return WISBLOCK_RUI3_BAND_AU915;
	case WISBLOCK_REGION_AS923_1:
		return WISBLOCK_RUI3_BAND_AS923_1;
	case WISBLOCK_REGION_AS923_2:
		return WISBLOCK_RUI3_BAND_AS923_2;
	case WISBLOCK_REGION_AS923_3:
		return WISBLOCK_RUI3_BAND_AS923_3;
	case WISBLOCK_REGION_AS923_4:
		return WISBLOCK_RUI3_BAND_AS923_4;
	case WISBLOCK_REGION_KR920:
		return WISBLOCK_RUI3_BAND_KR920;
	case WISBLOCK_REGION_IN865:
		return WISBLOCK_RUI3_BAND_IN865;
	case WISBLOCK_REGION_RU864:
		return WISBLOCK_RUI3_BAND_RU864;
	case WISBLOCK_REGION_CN470:
		return WISBLOCK_RUI3_BAND_CN470;
	default: // WISBLOCK_REGION_CN470_RP_1_0, WISBLOCK_REGION_WW2G4 - library-specific regions
			 // beyond RUI3's set
		return WISBLOCK_RUI3_BAND_UNKNOWN;
	}
}
