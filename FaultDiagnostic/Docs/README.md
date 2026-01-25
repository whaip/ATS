# FaultDiagnostic Runtime Orchestration

This folder now includes a lightweight system runtime & orchestration layer that wires:
- RTS (runtime execution/state machine)
- RMS (resource mapping)
- MTD (run logging/persistence)
- FRM (basic role-based authorization)

## Entry points
- `SystemRuntimeOrchestration` in `systemorchestration.h`.
- Access from `FaultDiagnostic::runtime()`.

## Typical wiring
1. Create/obtain a `JYThreadManager` and set it via `FaultDiagnostic::setRuntimeThreadManager(...)`.
2. Configure resource mappings in `runtime()->rms()->setMappings(...)`.
3. Set user role with `runtime()->frm()->setUser({id, role})`.
4. Start a run via `runtime()->rts()->startRun(...)`.

## Logs
Run logs are written to `runtime_logs/<board>_<runId>/runtime.jsonl` under the application directory.
