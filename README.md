# VillageSQL Slugify Extension

Generates URL-safe slugs from arbitrary text — folding accents, lowercasing, collapsing non-alphanumerics into a single separator, and trimming leading/trailing separators.

## Functions

### slugify

```sql
vsql_slugify.slugify(value TEXT) -> TEXT
```

Produces a URL-safe slug from the input text.

```sql
SELECT vsql_slugify.slugify('Hello, World!');                   -- 'hello-world'
SELECT vsql_slugify.slugify('Crème Brûlée — 2026 Edition');     -- 'creme-brulee-2026-edition'
SELECT vsql_slugify.slugify('café résumé naïve');               -- 'cafe-resume-naive'
SELECT vsql_slugify.slugify('München Straße');                  -- 'munchen-strasse'
SELECT vsql_slugify.slugify('  --Hello--World--  ');            -- 'hello-world'
```

## Usage in generated columns

```sql
CREATE TABLE articles (
    title TEXT,
    slug TEXT GENERATED ALWAYS AS (vsql_slugify.slugify(title)) STORED
);

INSERT INTO articles (title) VALUES ('Hello, World!');
SELECT title, slug FROM articles;
-- title: Hello, World!   slug: hello-world
```

## Installation

```sql
INSTALL EXTENSION vsql_slugify;
```

## Building from source

```bash
export VillageSQL_BUILD_DIR=/path/to/villagesql/build
./build.sh
```

## License

GPLv2 — see [LICENSE](LICENSE) for details.
