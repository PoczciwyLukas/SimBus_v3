#ifndef MCP23017_H
#define MCP23017_H

#include <stdint.h>

void MCP_Init(void);
void MCP_Poll(void);

/* Snapshot (na STATE_REQUEST) */
void MCP_EmitSnapshot(void);

/* Diagnostics */
uint32_t MCP_GetStableState(void);
uint32_t MCP_GetRawState(void);
uint32_t MCP_GetI2CErrCount(void);

#endif /* MCP23017_H */
