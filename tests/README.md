# tests

Tests that need neither the game nor a GPU, run by the CI on Linux and Windows (`.github/workflows/ci.yml`). The back
end's regression test runs over captures instead (`tools/test/`).

| File | What it checks |
|---|---|
| `test_setup.py` | The setup's command lines and files from folders with spaces and accents: the compiler's path on its way to the shader step (issue #29), the resume without `--source` after an update, the update's version order and its clean-up of `*.old` files |
| `test_issues.py` | The issue moderation's reading of a report (`tools/github/classify.py`): the release from the form, RecompLauncher's report or the prose, the GPU without the CPU's vendor, empty reports, logs, known errors and their regressions, which maintainer replies close an issue and which player replies reopen it |
| `test_flow.py` | Shapes of control flow ptxas leaves that the translator must structure as the branch graph runs them (`sm86/translate/flow.py`, checked with `flow_sim`): a jump into a tail that an if / else shares |
| `test_defaults.py` | The memory caps a player gets with nothing set (`runtime/gfx/vk/device/vk_defaults.c`), built alone and checked on the GPUs players reported: the Radeon 660M keeps its measured caps, a large discrete GPU is not held to them (issue #31) |

Usage: `uv run pytest tests`
