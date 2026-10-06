# Fixtures

Two small plugins with known provenance, built only to test the heuristic engine. Real plugins
cannot validate a detector: nobody can say for certain how a shipping binary was written, and the
commercial ones on this machine are encrypted anyway.

- **template-plugin** wears every fingerprint the engine looks for: JUCE's untouched template
  class names, a placeholder company and bundle id, a default version, the generic editor, scalar
  `std::sin` on the audio thread, and assistant-flavoured leftovers in its strings. It should
  score high.
- **crafted-plugin** does the same job - a tremolo - with renamed classes, real identity fields,
  a designed editor and a vectorised gain stage. It should score near zero.

Build them with `-DVIBECHECK_BUILD_FIXTURES=ON`; they are off by default because each one
compiles its own copy of JUCE.
