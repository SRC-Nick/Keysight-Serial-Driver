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
void UTAAPI SRCSerial_isOpen(HUTAPB hParmBlock);
void UTAAPI SRCSerial_getConfiguration(HUTAPB hParmBlock);
void UTAAPI SRCSerial_getDiagnostics(HUTAPB hParmBlock);
void UTAAPI SRCSerial_readUntilIdle(HUTAPB hParmBlock);
void UTAAPI SRCSerial_transact(HUTAPB hParmBlock);
void UTAAPI SRCSerial_writeHex(HUTAPB hParmBlock);
void UTAAPI SRCSerial_readHex(HUTAPB hParmBlock);
void UTAAPI SRCSerial_pulseControlLine(HUTAPB hParmBlock);
void UTAAPI SRCSerial_drainTransmit(HUTAPB hParmBlock);
void UTAAPI SRCSerial_enumeratePorts(HUTAPB hParmBlock);
void UTAAPI SRCSerial_cancel(HUTAPB hParmBlock);
void UTAAPI SRCSerial_getMoxaPortMode(HUTAPB hParmBlock);
void UTAAPI SRCSerial_setMoxaPortMode(HUTAPB hParmBlock);
void UTAAPI SRCSerial_workerStart(HUTAPB hParmBlock);
void UTAAPI SRCSerial_workerStop(HUTAPB hParmBlock);
void UTAAPI SRCSerial_workerGetStatus(HUTAPB hParmBlock);
void UTAAPI SRCSerial_workerReadEvents(HUTAPB hParmBlock);
void UTAAPI SRCSerial_workerQueueTx(HUTAPB hParmBlock);
void UTAAPI SRCSerial_cycleCreate(HUTAPB hParmBlock);
void UTAAPI SRCSerial_cycleUpdate(HUTAPB hParmBlock);
void UTAAPI SRCSerial_cycleDestroy(HUTAPB hParmBlock);
void UTAAPI SRCSerial_responseCreate(HUTAPB hParmBlock);
void UTAAPI SRCSerial_responseUpdate(HUTAPB hParmBlock);
void UTAAPI SRCSerial_responseDestroy(HUTAPB hParmBlock);
void UTAAPI SRCSerial_rxGetCount(HUTAPB hParmBlock);
void UTAAPI SRCSerial_rxReadFrame(HUTAPB hParmBlock);
void UTAAPI SRCSerial_rxClear(HUTAPB hParmBlock);
}
