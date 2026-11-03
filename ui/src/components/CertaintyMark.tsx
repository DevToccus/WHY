import { certaintyLabels } from "../format";
import type { Certainty } from "../types";

// The same three marks everywhere: full, half and empty. Shape carries the meaning, so colour is never the only signal.
export function CertaintyMark({ certainty, withLabel = false }: { certainty: Certainty; withLabel?: boolean }) {
  return (
    <span className={`certainty certainty-${certainty}`}>
      <svg viewBox="0 0 16 16" width="14" height="14" role="img" aria-label={certaintyLabels[certainty]}>
        <circle cx="8" cy="8" r="6.25" fill="none" stroke="currentColor" strokeWidth="1.5" />
        {certainty === "verified" && <circle cx="8" cy="8" r="6.25" fill="currentColor" />}
        {certainty === "probable" && <path d="M8 1.75a6.25 6.25 0 0 1 0 12.5z" fill="currentColor" />}
      </svg>
      {withLabel && <span>{certaintyLabels[certainty]}</span>}
    </span>
  );
}
