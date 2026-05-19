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

export const evalCases: EvalCase[] = [
  ...baseCases,
  ...diagnosticRepairCases,
  ...agencyOverrideCases,
];

export function findEvalCase(id: string): EvalCase | undefined {
  return evalCases.find((evalCase) => evalCase.id === id);
}
