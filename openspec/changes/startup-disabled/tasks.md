## Implementation

- [x] Execute cold/hot production startup in the host regression and observe failure.
- [x] Remove startup enable intent without changing thermal recovery.
- [x] Verify parameter interfaces and thermal protection; document the new behavior.
- [x] Run the complete host suite and Release build after this step (17 host checks passed).
- [ ] Verify physical power-on/reset PWM gating on an authorized target.

Host tests do not authorize motion. Keep this change unarchived until verification
and integration are ready; baseline specs still describe upstream behavior.
