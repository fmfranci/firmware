from pathlib import Path

path = Path("src/core/wifi/webInterface.cpp")
text = path.read_text(encoding="utf-8")

include_anchor = '#include "webInterface.h"\n'
include_line = '#include "rfWebApi.h"\n'
if include_line not in text:
    if include_anchor not in text:
        raise SystemExit("Could not find webInterface include anchor")
    text = text.replace(include_anchor, include_anchor + include_line, 1)

call_anchor = '    server->onNotFound(notFound);\n'
call_line = '    configureRfWebApi();\n'
if call_line not in text:
    if call_anchor not in text:
        raise SystemExit("Could not find configureWebServer anchor")
    text = text.replace(call_anchor, call_anchor + call_line, 1)

path.write_text(text, encoding="utf-8")
print("Applied CC1101 iPhone WebUI integration to", path)
