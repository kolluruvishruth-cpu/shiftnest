BEGIN;
-- Soft cancellation keeps assignment foreign keys and the original shift times.
ALTER TABLE shifts ADD COLUMN IF NOT EXISTS archived_at timestamptz;
-- Keep the solver's original measured result when a requirement is later cancelled.
ALTER TABLE optimization_runs ADD COLUMN IF NOT EXISTS original_details jsonb;
COMMIT;
