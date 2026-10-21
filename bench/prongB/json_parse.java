import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class json_parse {
    static final int JNULL = 0, JBOOL = 1, JNUM = 2, JSTR = 3, JARR = 4, JOBJ = 5;
    static final class J {
        int tag;
        long num;
        String str = "";
        ArrayList<J> vals = new ArrayList<>();
        ArrayList<String> keys = new ArrayList<>();
        J(int tag) { this.tag = tag; }
    }
    static String s;
    static int p;
    static void ws() {
        while (p < s.length()) {
            char c = s.charAt(p);
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            p++;
        }
    }
    static String parseString() {
        p++;
        StringBuilder b = new StringBuilder();
        while (p < s.length() && s.charAt(p) != '"') {
            char c = s.charAt(p);
            if (c == '\\') {
                p++;
                char e = s.charAt(p);
                if (e == 'n') b.append('\n');
                else if (e == 't') b.append('\t');
                else if (e == '"') b.append('"');
                else if (e == '\\') b.append('\\');
                else b.append(e);
            } else b.append(c);
            p++;
        }
        p++;
        return b.toString();
    }
    static J parseArray() {
        p++;
        J j = new J(JARR);
        ws();
        while (p < s.length() && s.charAt(p) != ']') {
            j.vals.add(parseValue());
            ws();
            if (p < s.length() && s.charAt(p) == ',') { p++; ws(); }
        }
        p++;
        return j;
    }
    static J parseObject() {
        p++;
        J j = new J(JOBJ);
        ws();
        while (p < s.length() && s.charAt(p) != '}') {
            String k = parseString();
            ws();
            p++;
            j.keys.add(k);
            j.vals.add(parseValue());
            ws();
            if (p < s.length() && s.charAt(p) == ',') { p++; ws(); }
        }
        p++;
        return j;
    }
    static J parseValue() {
        ws();
        char c = s.charAt(p);
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') { J j = new J(JSTR); j.str = parseString(); return j; }
        if (c == 't') { p += 4; J j = new J(JBOOL); j.num = 1; return j; }
        if (c == 'f') { p += 5; return new J(JBOOL); }
        if (c == 'n') { p += 4; return new J(JNULL); }
        boolean neg = false;
        if (s.charAt(p) == '-') { neg = true; p++; }
        long v = 0;
        while (p < s.length() && s.charAt(p) >= '0' && s.charAt(p) <= '9') {
            v = v * 10 + (s.charAt(p) - '0');
            p++;
        }
        J j = new J(JNUM);
        j.num = neg ? -v : v;
        return j;
    }
    static long walk(J j) {
        switch (j.tag) {
            case JNUM: return j.num + 1;
            case JSTR: return j.str.length() + 1;
            case JBOOL: case JNULL: return 1;
            case JARR: {
                long t = 1;
                for (J x : j.vals) t += walk(x);
                return t;
            }
            default: {
                long t = 1;
                for (String k : j.keys) t += k.length();
                for (J x : j.vals) t += walk(x);
                return t;
            }
        }
    }
    public static void main(String[] args) throws Exception {
        s = new String(System.in.readAllBytes(), StandardCharsets.UTF_8);
        long acc = 0;
        for (int k = 0; k < 30; k++) { p = 0; acc += walk(parseValue()); }
        System.out.println("checksum=" + acc);
    }
}
