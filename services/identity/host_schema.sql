CREATE TABLE accounts (
    id INTEGER PRIMARY KEY CHECK (id > 0),
    role INTEGER NOT NULL CHECK (role BETWEEN 0 AND 2),
    enabled INTEGER NOT NULL CHECK (enabled IN (0, 1))
) STRICT;

CREATE TABLE audit (
    sequence INTEGER PRIMARY KEY,
    time INTEGER NOT NULL,
    action INTEGER NOT NULL,
    actor INTEGER NOT NULL,
    target INTEGER NOT NULL,
    session INTEGER NOT NULL,
    accepted INTEGER NOT NULL CHECK (accepted IN (0, 1))
) STRICT;
