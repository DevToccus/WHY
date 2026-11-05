// Writes the invented sample as a report, so why.exe can show every kind of problem through Open report.
// Run with: npm --prefix ui run sample-report
import { writeFileSync } from "node:fs";
import { sample } from "../src/dev/devState.ts";

const out = process.argv[2] ?? "sample.whyreport.json";
writeFileSync(out, JSON.stringify({ ...sample, mode: "report", machine: { ...sample.machine, computerName: "" } }, null, 1));
console.log(`Sample report written to ${out}`);
