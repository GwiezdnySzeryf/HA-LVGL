# Interface translations

Each JSON file in this directory contains one complete interface translation.
File names use short BCP 47 language tags, for example `pl.json` and `en.json`.

## Update a translation

1. Edit only the values on the right side of each JSON property.
2. Keep keys, nesting, punctuation placeholders such as `{section}`, and JSON syntax unchanged.
3. Do not translate user data, entity IDs, URLs, MQTT topics, product names, or protocol names.
4. Run `node tools/validate-locales.js` from the repository root.
5. Open `www/index.html`, select the language, and check the result at desktop and mobile widths.

## Add a language

1. Copy `en.json` to a file named with the new BCP 47 language tag.
2. Translate every value without adding or removing keys.
3. Add the language tag to `supportedLanguages` in `www/i18n.js`.
4. Add its selector button to the language picker in `www/index.html`.
5. Run the locale validator.

Polish (`pl`) is the fallback language. Missing runtime translations fall back to Polish, but the validator treats missing keys as an error so incomplete translations do not reach users unnoticed.
