# Board database schema (BoardManager)

## Storage files
- `boards.json`: board metadata and component instances.
- `anchors.json`: anchor points per component (stored separately).
- `plan_bindings.json`: plan bindings per board, storing standard component parameters.

## Notes
- Legacy `plans.json` is read for migration if present.
- Component references are unified per board; new components get sequential numeric IDs when no reference is provided.
- In `BoardManager` UI, use “编辑锚点/编辑计划绑定” to update the JSON blocks for the selected board.
