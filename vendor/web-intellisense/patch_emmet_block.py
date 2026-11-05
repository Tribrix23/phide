with open(r'src/editor/IntelliSenseEngine.cpp', 'r', encoding='utf-8') as f:
    text = f.read().replace('\r\n', '\n')

old = """            // 2. Check for Emmet Abbreviation (Image 4: ul>li*3>span.hello$)
            std::wstring emmetPrefix = ExtractEmmetPrefix(currentLine, cursorCol);
            if (!emmetPrefix.empty() && emmetPrefix.length() >= 3) {
                std::wstring expanded = ExpandEmmet(emmetPrefix);
                if (!expanded.empty()) {
                    IntelliSenseItem eItem;
                    eItem.label = emmetPrefix;
                    eItem.detail = L"Emmet Abbreviation\\n" + expanded;
                    eItem.insertText = expanded;
                    eItem.kind = CompletionKind::Snippet;
                    eItem.origin = L"emmet";
                    eItem.score = 3500; // Highest priority at top of list!
                    rawCandidates.push_back(std::move(eItem));
                }
            }"""

new = """            // 2. Check for Emmet Abbreviation
            std::wstring emmetPrefix = ExtractEmmetPrefix(currentLine, cursorCol);
            if (!emmetPrefix.empty() && EmmetEngine::IsEmmetAbbreviation(emmetPrefix, false)) {
                std::wstring expanded = ExpandEmmet(emmetPrefix);
                if (!expanded.empty()) {
                    IntelliSenseItem eItem;
                    eItem.label = emmetPrefix;
                    eItem.detail = L"Emmet Abbreviation\\n" + expanded;
                    eItem.insertText = expanded;
                    eItem.kind = CompletionKind::Snippet;
                    eItem.origin = L"emmet";
                    eItem.score = 4000; // Highest priority at top of list!
                    rawCandidates.push_back(std::move(eItem));
                }
            }"""

if old in text:
    text = text.replace(old, new)
    with open(r'src/editor/IntelliSenseEngine.cpp', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Updated emmet completion block successfully!')
else:
    print('Target block not found!')
