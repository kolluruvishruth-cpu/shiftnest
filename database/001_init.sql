BEGIN;
CREATE TABLE IF NOT EXISTS settings(
 id int PRIMARY KEY CHECK(id=1), revision bigint NOT NULL DEFAULT 1,
 timezone text NOT NULL DEFAULT 'Asia/Kolkata', rest_minutes int NOT NULL DEFAULT 660 CHECK(rest_minutes BETWEEN 0 AND 1440),
 ot_threshold int NOT NULL DEFAULT 2400 CHECK(ot_threshold>0), ot_multiplier int NOT NULL DEFAULT 150 CHECK(ot_multiplier BETWEEN 100 AND 500),
 fair_weight int NOT NULL DEFAULT 100 CHECK(fair_weight BETWEEN 0 AND 10000)
);
INSERT INTO settings(id) VALUES(1) ON CONFLICT DO NOTHING;
CREATE TABLE IF NOT EXISTS users(id serial PRIMARY KEY,email text UNIQUE NOT NULL, password_hash text NOT NULL,role text NOT NULL CHECK(role IN('manager','employee')));
CREATE TABLE IF NOT EXISTS employees(id serial PRIMARY KEY,user_id int UNIQUE NOT NULL REFERENCES users(id), name text NOT NULL CHECK(length(name) BETWEEN 1 AND 100), rate int NOT NULL CHECK(rate BETWEEN 0 AND 10000000),max_minutes int NOT NULL CHECK(max_minutes BETWEEN 1 AND 10080),preferred int NOT NULL CHECK(preferred BETWEEN 1 AND 10080),active boolean NOT NULL DEFAULT true);
CREATE TABLE IF NOT EXISTS skills(id int PRIMARY KEY CHECK(id IN(1,2,4,8)),name text UNIQUE NOT NULL);
INSERT INTO skills VALUES(1,'Front desk'),(2,'Operations'),(4,'Support'),(8,'Supervisor') ON CONFLICT DO NOTHING;
CREATE TABLE IF NOT EXISTS employee_skills(employee_id int REFERENCES employees ON DELETE CASCADE,skill_id int REFERENCES skills,PRIMARY KEY(employee_id,skill_id));
CREATE TABLE IF NOT EXISTS availability(id serial PRIMARY KEY,employee_id int NOT NULL REFERENCES employees,starts timestamptz NOT NULL,ends timestamptz NOT NULL,CHECK(ends>starts),UNIQUE(employee_id,starts,ends));
CREATE TABLE IF NOT EXISTS shifts(id serial PRIMARY KEY,starts timestamptz NOT NULL,ends timestamptz NOT NULL,skill_id int NOT NULL REFERENCES skills,count int NOT NULL CHECK(count BETWEEN 1 AND 16),priority int NOT NULL DEFAULT 0 CHECK(priority BETWEEN 0 AND 10),CHECK(ends>starts AND ends-starts<=interval '24 hours'),CHECK(extract(second from starts)=0 AND extract(second from ends)=0));
CREATE TABLE IF NOT EXISTS schedule_versions(id serial PRIMARY KEY,week date NOT NULL CHECK(extract(isodow FROM week)=1),revision bigint NOT NULL,status text NOT NULL DEFAULT 'draft' CHECK(status IN('draft','published','superseded')),created_at timestamptz NOT NULL DEFAULT now(),published_at timestamptz,change_no int NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX IF NOT EXISTS one_published_week ON schedule_versions(week) WHERE status='published';
CREATE TABLE IF NOT EXISTS assignments(version_id int REFERENCES schedule_versions,shift_id int REFERENCES shifts ON DELETE RESTRICT,employee_id int REFERENCES employees,PRIMARY KEY(version_id,shift_id,employee_id));
CREATE TABLE IF NOT EXISTS optimization_runs(id serial PRIMARY KEY,version_id int UNIQUE REFERENCES schedule_versions,details jsonb NOT NULL,settings jsonb NOT NULL,created_at timestamptz DEFAULT now());
CREATE TABLE IF NOT EXISTS notifications(id serial PRIMARY KEY,user_id int NOT NULL REFERENCES users,version_id int REFERENCES schedule_versions,event_key text NOT NULL,message text NOT NULL,created_at timestamptz NOT NULL DEFAULT now(),read_at timestamptz,acknowledged_at timestamptz,UNIQUE(user_id,event_key));
CREATE TABLE IF NOT EXISTS sessions(token text PRIMARY KEY,user_id int NOT NULL REFERENCES users,csrf text NOT NULL,expires timestamptz NOT NULL);
CREATE TABLE IF NOT EXISTS login_attempts(key text PRIMARY KEY,failures int NOT NULL DEFAULT 0,window_start timestamptz NOT NULL DEFAULT now());
CREATE INDEX IF NOT EXISTS availability_employee ON availability(employee_id,starts,ends);
CREATE INDEX IF NOT EXISTS shifts_time ON shifts(starts);
CREATE INDEX IF NOT EXISTS notifications_user ON notifications(user_id,created_at DESC);
CREATE INDEX IF NOT EXISTS assignments_employee ON assignments(employee_id,version_id);
CREATE OR REPLACE FUNCTION bump_revision() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN UPDATE settings SET revision=revision+1 WHERE id=1; RETURN NULL; END $$;
DROP TRIGGER IF EXISTS employee_revision ON employees;
CREATE TRIGGER employee_revision AFTER INSERT OR UPDATE OR DELETE ON employees FOR EACH STATEMENT EXECUTE FUNCTION bump_revision();
DROP TRIGGER IF EXISTS skill_revision ON employee_skills;
CREATE TRIGGER skill_revision AFTER INSERT OR UPDATE OR DELETE ON employee_skills FOR EACH STATEMENT EXECUTE FUNCTION bump_revision();
DROP TRIGGER IF EXISTS availability_revision ON availability;
CREATE TRIGGER availability_revision AFTER INSERT OR UPDATE OR DELETE ON availability FOR EACH STATEMENT EXECUTE FUNCTION bump_revision();
DROP TRIGGER IF EXISTS shift_revision ON shifts;
CREATE TRIGGER shift_revision AFTER INSERT OR UPDATE OR DELETE ON shifts FOR EACH STATEMENT EXECUTE FUNCTION bump_revision();
CREATE OR REPLACE FUNCTION validate_shift_week() RETURNS trigger LANGUAGE plpgsql AS $$ DECLARE tz text; BEGIN SELECT timezone INTO tz FROM settings WHERE id=1; IF date_trunc('week',NEW.starts AT TIME ZONE tz)<>date_trunc('week',(NEW.ends-interval '1 second') AT TIME ZONE tz) THEN RAISE EXCEPTION 'Shift cannot cross the planning week boundary' USING ERRCODE='23514'; END IF; RETURN NEW; END $$;
DROP TRIGGER IF EXISTS shift_week ON shifts;
CREATE TRIGGER shift_week BEFORE INSERT OR UPDATE ON shifts FOR EACH ROW EXECUTE FUNCTION validate_shift_week();
COMMIT;
