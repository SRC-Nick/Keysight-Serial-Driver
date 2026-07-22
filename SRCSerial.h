#pragma once

#include "uta.h"

extern "C" {
void UTAAPI SRCSerial_start(HUTAPB hParmBlock);
void UTAAPI SRCSerial_stop(HUTAPB hParmBlock);
void UTAAPI SRCSerial_getBufferLength(HUTAPB hParmBlock);
void UTAAPI SRCSerial_readBytes(HUTAPB hParmBlock);
void UTAAPI SRCSerial_readString(HUTAPB hParmBlock);
void UTAAPI SRCSerial_writeBytes(HUTAPB hParmBlock);
void UTAAPI SRCSerial_writeString(HUTAPB hParmBlock);
void UTAAPI SRCSerial_flush(HUTAPB hParmBlock);
void UTAAPI SRCSerial_setControlLines(HUTAPB hParmBlock);
void UTAAPI SRCSerial_getLineStatus(HUTAPB hParmBlock);
}

