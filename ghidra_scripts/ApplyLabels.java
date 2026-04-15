// Apply labels from a JSON file to the current Ghidra project.
// Place JSON files next to this script in ghidra_scripts/.
//
// Usage:
//   Run via Script Manager (Window > Script Manager).
//   If one .json file exists next to the script, it loads automatically.
//   If multiple exist, a picker dialog appears.
//
// @category StarfieldRE

import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Function;

import generic.jar.ResourceFile;
import java.io.*;
import java.util.*;

public class ApplyLabels extends GhidraScript {

    // Simple JSON token types
    private static final int TOK_LBRACE = 1;
    private static final int TOK_RBRACE = 2;
    private static final int TOK_LBRACKET = 3;
    private static final int TOK_RBRACKET = 4;
    private static final int TOK_COLON = 5;
    private static final int TOK_COMMA = 6;
    private static final int TOK_STRING = 7;
    private static final int TOK_NUMBER = 8;
    private static final int TOK_TRUE = 9;
    private static final int TOK_FALSE = 10;
    private static final int TOK_NULL = 11;
    private static final int TOK_EOF = 12;

    // ── Minimal JSON parser (no external deps) ─────────────────

    private String jsonText;
    private int jsonPos;
    private int currentToken;
    private String currentString;
    private double currentNumber;

    private void jsonInit(String text) {
        this.jsonText = text;
        this.jsonPos = 0;
        jsonAdvance();
    }

    private void jsonSkipWhitespace() {
        while (jsonPos < jsonText.length() && Character.isWhitespace(jsonText.charAt(jsonPos))) {
            jsonPos++;
        }
    }

    private void jsonAdvance() {
        jsonSkipWhitespace();
        if (jsonPos >= jsonText.length()) {
            currentToken = TOK_EOF;
            return;
        }
        char c = jsonText.charAt(jsonPos);
        switch (c) {
            case '{': currentToken = TOK_LBRACE; jsonPos++; return;
            case '}': currentToken = TOK_RBRACE; jsonPos++; return;
            case '[': currentToken = TOK_LBRACKET; jsonPos++; return;
            case ']': currentToken = TOK_RBRACKET; jsonPos++; return;
            case ':': currentToken = TOK_COLON; jsonPos++; return;
            case ',': currentToken = TOK_COMMA; jsonPos++; return;
            case '"': currentToken = TOK_STRING; currentString = jsonReadString(); return;
            case 't':
                jsonExpect("true"); currentToken = TOK_TRUE; return;
            case 'f':
                jsonExpect("false"); currentToken = TOK_FALSE; return;
            case 'n':
                jsonExpect("null"); currentToken = TOK_NULL; return;
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    currentToken = TOK_NUMBER;
                    currentNumber = jsonReadNumber();
                    return;
                }
                throw new RuntimeException("Unexpected char '" + c + "' at pos " + jsonPos);
        }
    }

    private void jsonExpect(String keyword) {
        for (int i = 0; i < keyword.length(); i++) {
            if (jsonPos >= jsonText.length() || jsonText.charAt(jsonPos) != keyword.charAt(i)) {
                throw new RuntimeException("Expected '" + keyword + "' at pos " + jsonPos);
            }
            jsonPos++;
        }
    }

    private String jsonReadString() {
        jsonPos++; // skip opening quote
        StringBuilder sb = new StringBuilder();
        while (jsonPos < jsonText.length()) {
            char c = jsonText.charAt(jsonPos);
            if (c == '"') {
                jsonPos++;
                return sb.toString();
            }
            if (c == '\\') {
                jsonPos++;
                if (jsonPos >= jsonText.length()) break;
                char esc = jsonText.charAt(jsonPos);
                switch (esc) {
                    case '"': sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case '/': sb.append('/'); break;
                    case 'n': sb.append('\n'); break;
                    case 'r': sb.append('\r'); break;
                    case 't': sb.append('\t'); break;
                    case 'u':
                        String hex = jsonText.substring(jsonPos + 1, jsonPos + 5);
                        sb.append((char) Integer.parseInt(hex, 16));
                        jsonPos += 4;
                        break;
                    default: sb.append(esc);
                }
            } else {
                sb.append(c);
            }
            jsonPos++;
        }
        throw new RuntimeException("Unterminated string");
    }

    private double jsonReadNumber() {
        int start = jsonPos;
        if (jsonPos < jsonText.length() && jsonText.charAt(jsonPos) == '-') jsonPos++;
        while (jsonPos < jsonText.length() && jsonText.charAt(jsonPos) >= '0' && jsonText.charAt(jsonPos) <= '9') jsonPos++;
        if (jsonPos < jsonText.length() && jsonText.charAt(jsonPos) == '.') {
            jsonPos++;
            while (jsonPos < jsonText.length() && jsonText.charAt(jsonPos) >= '0' && jsonText.charAt(jsonPos) <= '9') jsonPos++;
        }
        if (jsonPos < jsonText.length() && (jsonText.charAt(jsonPos) == 'e' || jsonText.charAt(jsonPos) == 'E')) {
            jsonPos++;
            if (jsonPos < jsonText.length() && (jsonText.charAt(jsonPos) == '+' || jsonText.charAt(jsonPos) == '-')) jsonPos++;
            while (jsonPos < jsonText.length() && jsonText.charAt(jsonPos) >= '0' && jsonText.charAt(jsonPos) <= '9') jsonPos++;
        }
        return Double.parseDouble(jsonText.substring(start, jsonPos));
    }

    // Returns: Map for objects, List for arrays, String, Double, Boolean, or null
    private Object jsonParseValue() {
        switch (currentToken) {
            case TOK_LBRACE: return jsonParseObject();
            case TOK_LBRACKET: return jsonParseArray();
            case TOK_STRING: { String s = currentString; jsonAdvance(); return s; }
            case TOK_NUMBER: { Double d = currentNumber; jsonAdvance(); return d; }
            case TOK_TRUE: jsonAdvance(); return Boolean.TRUE;
            case TOK_FALSE: jsonAdvance(); return Boolean.FALSE;
            case TOK_NULL: jsonAdvance(); return null;
            default: throw new RuntimeException("Unexpected token " + currentToken + " at pos " + jsonPos);
        }
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> jsonParseObject() {
        jsonAdvance(); // skip {
        Map<String, Object> map = new LinkedHashMap<>();
        if (currentToken == TOK_RBRACE) { jsonAdvance(); return map; }
        while (true) {
            if (currentToken != TOK_STRING) throw new RuntimeException("Expected string key at pos " + jsonPos);
            String key = currentString;
            jsonAdvance(); // skip key
            if (currentToken != TOK_COLON) throw new RuntimeException("Expected ':' at pos " + jsonPos);
            jsonAdvance(); // skip :
            map.put(key, jsonParseValue());
            if (currentToken == TOK_COMMA) { jsonAdvance(); continue; }
            if (currentToken == TOK_RBRACE) { jsonAdvance(); return map; }
            throw new RuntimeException("Expected ',' or '}' at pos " + jsonPos);
        }
    }

    @SuppressWarnings("unchecked")
    private List<Object> jsonParseArray() {
        jsonAdvance(); // skip [
        List<Object> list = new ArrayList<>();
        if (currentToken == TOK_RBRACKET) { jsonAdvance(); return list; }
        while (true) {
            list.add(jsonParseValue());
            if (currentToken == TOK_COMMA) { jsonAdvance(); continue; }
            if (currentToken == TOK_RBRACKET) { jsonAdvance(); return list; }
            throw new RuntimeException("Expected ',' or ']' at pos " + jsonPos);
        }
    }

    @SuppressWarnings("unchecked")
    private Map<String, Object> parseJson(String text) {
        jsonInit(text);
        return (Map<String, Object>) jsonParseValue();
    }

    // ── Helpers for reading JSON fields ────────────────────────

    @SuppressWarnings("unchecked")
    private static String str(Map<String, Object> m, String key) {
        Object v = m.get(key);
        return v != null ? v.toString() : null;
    }

    @SuppressWarnings("unchecked")
    private static String str(Map<String, Object> m, String key, String def) {
        Object v = m.get(key);
        return v != null ? v.toString() : def;
    }

    @SuppressWarnings("unchecked")
    private static List<Map<String, Object>> list(Map<String, Object> m, String key) {
        Object v = m.get(key);
        if (v == null) return Collections.emptyList();
        return (List<Map<String, Object>>) (List<?>) v;
    }

    // ── Ghidra operations ──────────────────────────────────────

    private Address hexToAddr(String hex) {
        long offset = Long.parseUnsignedLong(hex.replace("0x", "").replace("0X", ""), 16);
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset);
    }

    private void renameData(String addrStr, String name) throws Exception {
        Address a = hexToAddr(addrStr);
        var sym = currentProgram.getSymbolTable().getPrimarySymbol(a);
        if (sym != null) {
            sym.setName(name, SourceType.USER_DEFINED);
        } else {
            currentProgram.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED);
        }
        println("  data  " + addrStr + " -> " + name);
    }

    private void renameFunc(String addrStr, String name) throws Exception {
        Address a = hexToAddr(addrStr);
        Function fn = currentProgram.getFunctionManager().getFunctionAt(a);
        if (fn != null) {
            fn.setName(name, SourceType.USER_DEFINED);
            println("  func  " + addrStr + " -> " + name);
        } else {
            println("  WARN  no function at " + addrStr + " for " + name);
        }
    }

    private void setComment(String addrStr, String text, String kind) {
        CommentType commentType;
        if ("plate".equals(kind)) commentType = CommentType.PLATE;
        else if ("pre".equals(kind)) commentType = CommentType.PRE;
        else if ("post".equals(kind)) commentType = CommentType.POST;
        else if ("repeatable".equals(kind)) commentType = CommentType.REPEATABLE;
        else commentType = CommentType.EOL;

        Address a = hexToAddr(addrStr);
        CodeUnit cu = currentProgram.getListing().getCodeUnitAt(a);
        if (cu == null) cu = currentProgram.getListing().getCodeUnitContaining(a);
        if (cu != null) cu.setComment(commentType, text);
    }

    private DataType resolveType(String typeName) {
        if (typeName == null) return Undefined8DataType.dataType;
        switch (typeName) {
            case "pointer": return PointerDataType.dataType;
            case "bool":    return BooleanDataType.dataType;
            case "int":     return IntegerDataType.dataType;
            case "uint":    return UnsignedIntegerDataType.dataType;
            case "int64":   return LongDataType.dataType;
            case "uint64":  return UnsignedLongLongDataType.dataType;
            case "short":   return ShortDataType.dataType;
            case "float":   return FloatDataType.dataType;
            case "double":  return DoubleDataType.dataType;
            case "char":    return CharDataType.dataType;
            case "byte":    return Undefined1DataType.dataType;
            case "word":    return Undefined2DataType.dataType;
            case "dword":   return Undefined4DataType.dataType;
            case "qword":   return Undefined8DataType.dataType;
            default:
                println("  WARN  unknown type '" + typeName + "', using qword");
                return Undefined8DataType.dataType;
        }
    }

    private void createStruct(Map<String, Object> def) {
        String name = str(def, "name");
        String sizeStr = str(def, "size");
        long size = Long.parseLong(sizeStr.replace("0x", "").replace("0X", ""), 16);
        List<Map<String, Object>> fields = list(def, "fields");

        CategoryPath cat = new CategoryPath("/StarfieldRE");
        StructureDataType struct = new StructureDataType(cat, name, (int) size);

        for (Map<String, Object> f : fields) {
            String offStr = str(f, "offset");
            int off = Integer.parseInt(offStr.replace("0x", "").replace("0X", ""), 16);
            DataType dt = resolveType(str(f, "type"));
            String fieldName = str(f, "name", "field_" + offStr);
            String comment = str(f, "comment");
            struct.replaceAtOffset(off, dt, dt.getLength(), fieldName, comment);
        }

        var dtm = currentProgram.getDataTypeManager();
        DataType existing = dtm.getDataType(cat, name);
        if (existing != null) {
            dtm.remove(existing);
        }
        dtm.addDataType(struct, null);
        println("  struct " + name + " (0x" + Long.toHexString(size) + " bytes, " + fields.size() + " fields)");
    }

    // ── JSON file discovery ────────────────────────────────────

    private File pickJsonFile() throws Exception {
        ResourceFile scriptRes = getSourceFile();
        File scriptDir = scriptRes.getParentFile().getFile(false);

        List<File> jsonFiles = new ArrayList<>();
        File[] files = scriptDir.listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.getName().endsWith(".json")) {
                    jsonFiles.add(f);
                }
            }
        }

        if (jsonFiles.isEmpty()) {
            printerr("No .json files found in " + scriptDir.getAbsolutePath());
            return null;
        }
        if (jsonFiles.size() == 1) {
            return jsonFiles.get(0);
        }

        // Multiple files - show picker
        List<String> names = new ArrayList<>();
        for (File f : jsonFiles) names.add(f.getName());
        String choice = askChoice("Select Label File", "Which JSON file to apply?", names, names.get(0));
        int idx = names.indexOf(choice);
        return jsonFiles.get(idx);
    }

    private String readFile(File f) throws Exception {
        StringBuilder sb = new StringBuilder();
        BufferedReader reader = new BufferedReader(new FileReader(f));
        try {
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append('\n');
            }
        } finally {
            reader.close();
        }
        return sb.toString();
    }

    // ── Entry point ────────────────────────────────────────────

    @Override
    public void run() throws Exception {
        File jsonFile = pickJsonFile();
        if (jsonFile == null) return;

        println("=== Applying labels from: " + jsonFile.getName() + " ===");

        Map<String, Object> data = parseJson(readFile(jsonFile));

        String version = str(data, "version");
        if (version != null) {
            println("Target game version: " + version);
        }
        println("");

        int total = 0;

        // Structs
        List<Map<String, Object>> structs = list(data, "structs");
        if (!structs.isEmpty()) {
            println("[Structs] (" + structs.size() + ")");
            for (Map<String, Object> s : structs) createStruct(s);
            println("");
            total += structs.size();
        }

        // Globals
        List<Map<String, Object>> globals = list(data, "globals");
        if (!globals.isEmpty()) {
            println("[Globals] (" + globals.size() + ")");
            for (Map<String, Object> g : globals) {
                renameData(str(g, "address"), str(g, "name"));
                String comment = str(g, "comment");
                if (comment != null) setComment(str(g, "address"), comment, "eol");
            }
            println("");
            total += globals.size();
        }

        // Functions
        List<Map<String, Object>> funcs = list(data, "functions");
        if (!funcs.isEmpty()) {
            println("[Functions] (" + funcs.size() + ")");
            for (Map<String, Object> fn : funcs) {
                renameFunc(str(fn, "address"), str(fn, "name"));
                String comment = str(fn, "comment");
                if (comment != null) {
                    setComment(str(fn, "address"), comment, str(fn, "commentType", "plate"));
                }
            }
            println("");
            total += funcs.size();
        }

        println("=== Done! Applied " + total + " entries. Ctrl+S to save. ===");
    }
}
