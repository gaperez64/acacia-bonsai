import Lean.CoreM
import Lean.Util.CollectAxioms

open Lean

-- Select by defining module, so private and generated declarations count too.
def auditAxioms (modules : Array Name) : CoreM Nat := do
  let env ← getEnv
  let mut checked := 0
  for (name, _) in env.constants do
    if let some idx := env.getModuleIdxFor? name then
      if modules.contains env.header.moduleNames[idx]! then
        checked := checked + 1
        for ax in ← collectAxioms name do
          unless #[``propext, ``Classical.choice, ``Quot.sound].contains ax do
            throwError "{name} depends on forbidden axiom {ax}"
  -- An import-only root can have no declarations of its own.
  return checked

def main (args : List String) : IO Unit := do
  if args.isEmpty then
    throw <| IO.userError "usage: lean --run AuditAxioms.lean MODULE ..."
  initSearchPath (← findSysroot)
  let modules := args.toArray.map String.toName
  unsafe enableInitializersExecution
  let env ← importModules (modules.map fun name => { module := name }) {} (loadExts := true) (level := .private)
  let count ← (auditAxioms modules).toIO' { fileName := "<axiom audit>", fileMap := default } { env }
  IO.println s!"audited axioms of {count} declarations in {modules.size} project modules"
