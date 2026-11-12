BEGIN;
ALTER TABLE shifts ADD COLUMN IF NOT EXISTS title text NOT NULL DEFAULT '';
ALTER TABLE shifts DROP CONSTRAINT IF EXISTS shifts_title_check;
ALTER TABLE shifts ADD CONSTRAINT shifts_title_check CHECK(length(title)<=100);
-- The C optimizer stores skill eligibility as a positive 30-bit mask.
ALTER TABLE skills DROP CONSTRAINT IF EXISTS skills_id_check;
ALTER TABLE skills DROP CONSTRAINT IF EXISTS skills_power_of_two_check;
ALTER TABLE skills ADD CONSTRAINT skills_power_of_two_check
 CHECK(id>0 AND id<=536870912 AND (id & (id-1))=0);
ALTER TABLE skills DROP CONSTRAINT IF EXISTS skills_name_check;
ALTER TABLE skills ADD CONSTRAINT skills_name_check CHECK(length(btrim(name)) BETWEEN 1 AND 100);
CREATE UNIQUE INDEX IF NOT EXISTS skills_name_normalized ON skills(lower(btrim(name)));
COMMIT;
