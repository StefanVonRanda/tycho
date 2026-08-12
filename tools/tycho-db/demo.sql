CREATE TABLE users (id INT, name TEXT, age INT);
INSERT INTO users VALUES (1, 'ada', 36);
INSERT INTO users VALUES (2, 'bob', 41);
INSERT INTO users VALUES (3, 'cy', 29);
SELECT name, age FROM users WHERE age > 30 AND name != 'bob';
SELECT * FROM users;
