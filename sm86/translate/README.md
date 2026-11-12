# sm86/translate

SM86 → GLSL 4.6 translator, offline (`glslc` makes the SPIR-V).

| File | What it does |
|---|---|
| `program.py` | Loads a program: SPH (0x80-byte header, layout from Ryujinx `ShaderHeader.cs`, MIT) and its instructions decoded with `sm86/decode`; stops at the data that follows the code |
| `flow_sim.py` | Simulates off the GPU the original branch graph and the structured GLSL with the same pseudo-random answers for each condition and compares the instructions they execute; `flow.structure` rejects (and keeps the `switch` for) what does not match over 4 sets of answers |
| `flow.py` | Structured control flow: `if/else` and `do/while` from the `BRA`s instead of a `switch` on the PC (idea and cases from SavageWindRecomp `structure_flow`, MIT); also a `CALL` without `RET` (a jump), branches to the far join of an if / else-if chain and the conditional exit just before the back edge (`while (!P)`); a jump into a short tail that ptxas shares between the branches of an if / else gets its own copy of it (`_dup_tails`, checked by `flow_sim` against the original graph); if the branches do not nest, or there is CALL/RET (except a subroutine called once, which is inlined) or BRX, `glsl/core.py` keeps the `switch` |
| `glsl/` | The GLSL translator split by responsibility (details in [glsl/README.md](glsl/README.md)): `uint` registers, `bool` predicates, uniform registers, structured control flow (`flow.py`) or, when not possible, a `switch` dispatcher on the PC, constant banks as UBOs, global memory as storage buffers (base in `c[0]`), textures by their handle `c[bank][idx]`, PS inputs/outputs from the SPH. Semantics from Ryujinx and Mesa NAK (MIT), cited in each function. What it does not handle raises `Unsupported` with the class |
