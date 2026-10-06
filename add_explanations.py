import re

with open('src/vibecheck/Heuristics.cpp', 'r') as f:
    content = f.read()

replacements = [
    (r'add \(Family::metadata, "no manufacturer name at all", "\([^"]+\)", 18\.0\);',
     'add (Family::metadata, "no manufacturer name at all", "(empty)", "Commercial or properly branded plugins usually set a manufacturer name. AI generators often leave it blank.", 18.0);'),
    (r'add \(Family::metadata, "placeholder manufacturer name", description\.manufacturerName, 18\.0\);',
     'add (Family::metadata, "placeholder manufacturer name", description.manufacturerName, "A placeholder like \'yourcompany\' is a strong indicator of an unmodified generated template.", 18.0);'),
    (r'add \(Family::metadata, "template-default plugin name", description\.name, 12\.0\);',
     'add (Family::metadata, "template-default plugin name", description.name, "Using a generic default name for the plugin project often means the developer skipped setup steps.", 12.0);'),
    (r'add \(Family::metadata, "untouched default version string", description\.version\.isEmpty\(\) \? "\(empty\)" : description\.version, 5\.0\);',
     'add (Family::metadata, "untouched default version string", description.version.isEmpty() ? "(empty)" : description.version, "AI-generated plugins rarely manage versioning beyond the default 1.0.0.", 5.0);'),
    (r'add \(Family::metadata, "placeholder bundle identifier", "com\.yourcompany \/ com\.example", 20\.0\);',
     'add (Family::metadata, "placeholder bundle identifier", "com.yourcompany / com.example", "macOS plugins use reverse-DNS bundle IDs. Unchanged placeholders indicate a raw generated template.", 20.0);'),
    (r'add \(Family::boilerplate, "untouched JUCE template class name", symbol, 22\.0\);',
     'add (Family::boilerplate, "untouched JUCE template class name", symbol, "JUCE creates standard classes like \'NewProjectAudioProcessor\'. Leaving these unmodified suggests code was generated and compiled without manual structuring.", 22.0);'),
    (r'add \(Family::boilerplate, "template source file names left in the binary", templateFile, 10\.0\);',
     'add (Family::boilerplate, "template source file names left in the binary", templateFile, "Generated JUCE projects use default filenames. Human developers typically rename files to match their plugin\'s architecture.", 10.0);'),
    (r'add \(Family::boilerplate, "AI-style ALL CAPS parameter IDs paired with labels", foundPairs\.joinIntoString \(", "\), 40\.0\);',
     'add (Family::boilerplate, "AI-style ALL CAPS parameter IDs paired with labels", foundPairs.joinIntoString (", "), "AI tools frequently hallucinate parameter IDs entirely in uppercase (e.g. CUTOFF instead of cutoff_hz), which is a non-standard convention.", 40.0);'),
    (r'add \(Family::boilerplate, "AI-style ALL CAPS parameter IDs", combined\.joinIntoString \(", "\), 35\.0\);',
     'add (Family::boilerplate, "AI-style ALL CAPS parameter IDs", combined.joinIntoString (", "), "AI tools frequently hallucinate parameter IDs entirely in uppercase, which is a non-standard convention.", 35.0);'),
    (r'add \(Family::boilerplate, "ALL CAPS strings matching common audio parameter names", standaloneIds\.joinIntoString \(", "\), 30\.0\);',
     'add (Family::boilerplate, "ALL CAPS strings matching common audio parameter names", standaloneIds.joinIntoString (", "), "AI tools frequently use uppercase parameter names internally.", 30.0);'),
    (r'add \(Family::boilerplate, "ALL CAPS parameter ID detected", combined\.joinIntoString \(", "\), 15\.0\);',
     'add (Family::boilerplate, "ALL CAPS parameter ID detected", combined.joinIntoString (", "), "An uppercase parameter ID was found, often an artifact of AI code generation.", 15.0);'),
    (r'add \(Family::boilerplate, "default AudioProcessorValueTreeState scaffolding", "createParameterLayout\(\) \+ \\"Parameters\\"", 35\.0\);',
     'add (Family::boilerplate, "default AudioProcessorValueTreeState scaffolding", "createParameterLayout() + \\"Parameters\\"", "AI typically uses the default boilerplate exact parameter tree ID \'Parameters\'.", 35.0);'),
    (r'add \(Family::dspNaivety, "getRawParameterValue\(\) used \(often unsafely polled in processBlock by LLMs\)", "getRawParameterValue", 25\.0\);',
     'add (Family::dspNaivety, "getRawParameterValue() used (often unsafely polled in processBlock by LLMs)", "getRawParameterValue", "AI models frequently write DSP code that unsafely polls atomic parameter pointers directly in the audio loop rather than caching them.", 25.0);'),
    (r'add \(Family::boilerplate, "empty getStateInformation\/setStateInformation stubs \(plugin cannot save\/recall state\)",\s+facts\.stripped \? "unstripped symbols" : "stubs still in binary",\s+30\.0\);',
     'add (Family::boilerplate, "empty getStateInformation/setStateInformation stubs (plugin cannot save/recall state)",\n                 facts.stripped ? "unstripped symbols" : "stubs still in binary", "LLMs often generate standard framework functions but leave them empty, meaning the plugin\'s state cannot be saved or restored.",\n                 30.0);'),
    (r'add \(Family::boilerplate, "BinaryData::bg_jpg background image \(common AI generator pattern\)", "bg_jpg", 15\.0\);',
     'add (Family::boilerplate, "BinaryData::bg_jpg background image (common AI generator pattern)", "bg_jpg", "AI generators frequently hallucinate embedding a background image named \'bg.jpg\' into the UI.", 15.0);'),
    (r'add \(Family::boilerplate, "editor uses Timer for repaint polling \(common AI metering pattern\)",\s+"timerCallback",\s+20\.0\);',
     'add (Family::boilerplate, "editor uses Timer for repaint polling (common AI metering pattern)",\n                 "timerCallback", "AI commonly generates primitive UIs that use a generic timer to poll and repaint the whole screen constantly, instead of proper MVC callbacks.",\n                 20.0);'),
    (r'add \(Family::boilerplate, "boilerplate program management stubs \(getNumPrograms, changeProgramName\)",\s+facts\.stripped \? "unstripped symbols" : "stubs still in binary",\s+5\.0\);',
     'add (Family::boilerplate, "boilerplate program management stubs (getNumPrograms, changeProgramName)",\n                 facts.stripped ? "unstripped symbols" : "stubs still in binary", "These legacy VST2 program functions are required by JUCE but usually left empty by AI generators.",\n                 5.0);'),
    (r'add \(Family::dspNaivety, "scalar libm maths with no vectorised path anywhere",\s+"sinf, cosf, powf etc but no SIMD",\s+15\.0\);',
     'add (Family::dspNaivety, "scalar libm maths with no vectorised path anywhere",\n             "sinf, cosf, powf etc but no SIMD", "Most modern handwritten plugins use vectorised maths for performance. AI code is typically naive and relies solely on scalar standard library maths functions.",\n             15.0);'),
    (r'add \(Family::stringArtifact, "text that reads like it came from a chat assistant", artifact, 30\.0\);',
     'add (Family::stringArtifact, "text that reads like it came from a chat assistant", artifact, "The binary contains strings like \'Here is the C++ code\', which is an unmistakable tell of AI chat output pasted directly into the source.", 30.0);'),
]

for pat, rep in replacements:
    content, n = re.subn(pat, rep, content)
    if n == 0:
        print(f"Warning: could not match {pat}")

with open('src/vibecheck/Heuristics.cpp', 'w') as f:
    f.write(content)
