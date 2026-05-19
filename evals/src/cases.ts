export interface EvalCase {
  id: string;
  title: string;
  prompt: string;
  fixtureSource: string;
  expectedStdout: string;
  requiredSourcePatterns: RegExp[];
}

import { evalCases as baseCases } from "./cases/base.js";
import { diagnosticRepairCases } from "./cases/diagnostic-repair.js";
import { agencyOverrideCases } from "./cases/agency-override.js";
import { zeroShotCases } from "./cases/zero-shot.js";
import { zeroShotRepairCases } from "./cases/zero-shot-repair.js";

export const evalCases: EvalCase[] = [
  ...baseCases,
  ...diagnosticRepairCases,
  ...agencyOverrideCases,
  ...zeroShotCases,
  ...zeroShotRepairCases,
];

export function findEvalCase(id: string): EvalCase | undefined {
  return evalCases.find((evalCase) => evalCase.id === id);
}
