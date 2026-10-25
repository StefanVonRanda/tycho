// json tree, Java: materialize one generic tagged tree, then fold it.
public class json {
    static int n = 50000;

    static final class Json {
        int kind;                 // 0 null, 1 bool, 2 num, 3 str, 4 arr, 5 obj
        long num;
        String str;
        Json[] items;
        String[] keys;
        Json[] vals;
        int len;
        Json(int k) { kind = k; }
    }

    static String doc;
    static int pos;

    static void ws() {
        while (pos < doc.length()) {
            char c = doc.charAt(pos);
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
            pos++;
        }
    }

    static String string() {
        pos++;
        int start = pos;
        while (pos < doc.length() && doc.charAt(pos) != '"') pos++;
        String r = doc.substring(start, pos);
        pos++;
        return r;
    }

    static Json number() {
        int start = pos;
        if (doc.charAt(pos) == '-') pos++;
        while (pos < doc.length() && Character.isDigit(doc.charAt(pos))) pos++;
        Json j = new Json(2);
        j.num = Long.parseLong(doc.substring(start, pos));
        return j;
    }

    static Json value() {
        ws();
        char c = doc.charAt(pos);
        if (c == '{') {
            pos++; ws();
            Json j = new Json(5);
            String[] keys = new String[4];
            Json[] vals = new Json[4];
            int len = 0;
            while (pos < doc.length() && doc.charAt(pos) != '}') {
                String k = string();
                ws();
                if (doc.charAt(pos) == ':') pos++;
                Json v = value();
                if (len == keys.length) {
                    keys = java.util.Arrays.copyOf(keys, len * 2);
                    vals = java.util.Arrays.copyOf(vals, len * 2);
                }
                keys[len] = k;
                vals[len++] = v;
                ws();
                if (doc.charAt(pos) == ',') { pos++; ws(); }
            }
            if (pos < doc.length()) pos++;
            j.keys = keys; j.vals = vals; j.len = len;
            return j;
        }
        if (c == '[') {
            pos++; ws();
            Json j = new Json(4);
            Json[] items = new Json[4];
            int len = 0;
            while (pos < doc.length() && doc.charAt(pos) != ']') {
                if (len == items.length) items = java.util.Arrays.copyOf(items, len * 2);
                items[len++] = value();
                ws();
                if (doc.charAt(pos) == ',') { pos++; ws(); }
            }
            if (pos < doc.length()) pos++;
            j.items = items; j.len = len;
            return j;
        }
        if (c == '"') { Json j = new Json(3); j.str = string(); return j; }
        if (c == 't') { pos += 4; Json j = new Json(1); j.num = 1; return j; }
        if (c == 'f') { pos += 5; return new Json(1); }
        if (c == 'n') { pos += 4; return new Json(0); }
        return number();
    }

    static Json get(Json j, String key) {
        for (int i = 0; i < j.len; i++) if (j.keys[i].equals(key)) return j.vals[i];
        return new Json(0);
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder(1 << 20);
        sb.append('[');
        for (int i = 0; i < n; i++) {
            if (i > 0) sb.append(',');
            sb.append("{\"id\":").append(i)
              .append(",\"cat\":").append(i % 32)
              .append(",\"amt\":").append((i * 2654435761L) % 1000)
              .append(",\"name\":\"u").append(i % 1000).append("\"}");
        }
        sb.append(']');
        doc = sb.toString();
        pos = 0;
        Json root = value();
        long acc = 0;
        for (int i = 0; i < root.len; i++) {
            Json o = root.items[i];
            acc += get(o, "id").num + get(o, "cat").num + get(o, "amt").num
                 + get(o, "name").str.length();
        }
        System.out.println(acc);
    }
}
