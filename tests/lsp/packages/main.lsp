{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}}
{"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "$URI", "languageId": "ofir", "version": 1, "text": $TEXT}}}
{"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 15}}}
{"jsonrpc": "2.0", "id": 3, "method": "textDocument/hover", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 20}}}
{"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "$DIR/deps/shapes/src/index.of", "languageId": "ofir", "version": 1, "text": "pub fn area(side: int, scale: int) int {\n    return side * side * scale\n}\n"}}}
{"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {"textDocument": {"uri": "$URI", "version": 2}, "contentChanges": [{"text": $TEXT}]}}
{"jsonrpc": "2.0", "id": 4, "method": "textDocument/references", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 15}, "context": {"includeDeclaration": true}}}
{"jsonrpc": "2.0", "id": 5, "method": "textDocument/references", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 4, "character": 10}, "context": {"includeDeclaration": false}}}
{"jsonrpc": "2.0", "id": 6, "method": "textDocument/rename", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 15}, "newName": "squareArea"}}
{"jsonrpc": "2.0", "id": 7, "method": "textDocument/rename", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 15}, "newName": "fn"}}
{"jsonrpc": "2.0", "id": 8, "method": "textDocument/rename", "params": {"textDocument": {"uri": "$URI"}, "position": {"line": 5, "character": 5}, "newName": "show"}}
{"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": "$DIR/deps/shapes/src/index.of"}}}
{"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {"textDocument": {"uri": "$URI", "version": 3}, "contentChanges": [{"text": $TEXT}]}}
{"jsonrpc": "2.0", "id": 99, "method": "shutdown"}
{"jsonrpc": "2.0", "method": "exit"}
