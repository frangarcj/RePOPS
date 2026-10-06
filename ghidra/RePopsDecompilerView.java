// Decompiler-only register-base view. Does not modify ProgramDB's compiler
// selection, the installed Allegrex extension or instruction bytes.
import ghidra.app.plugin.processors.sleigh.SleighLanguage;
import ghidra.program.model.lang.BasicCompilerSpec;
import ghidra.program.model.lang.CompilerSpec;
import ghidra.program.model.listing.Program;
import ghidra.program.model.pcode.XmlEncode;
import java.io.ByteArrayInputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Pattern;

public final class RePopsDecompilerView {
    private RePopsDecompilerView() {}

    public static Program withoutGpSpacebase(Program program, Path out) throws Exception {
        XmlEncode xml = new XmlEncode(false);
        CompilerSpec original = program.getCompilerSpec();
        original.encode(xml);
        String source = xml.toString();
        Pattern base = Pattern.compile("<spacebase\\b[^>]*\\bname=\"gp\"[^>]*/>");
        Pattern range = Pattern.compile("<range\\b[^>]*\\bspace=\"gp\"[^>]*/>");
        if (base.matcher(source).results().count() != 1 || range.matcher(source).results().count() != 1)
            throw new IllegalStateException("Expected exactly one Allegrex GP spacebase and global range");
        String modified = range.matcher(base.matcher(source).replaceFirst("")).replaceFirst("");
        BasicCompilerSpec view = new BasicCompilerSpec(original.getCompilerSpecDescription(),
                (SleighLanguage)program.getLanguage(),
                new ByteArrayInputStream(modified.getBytes(StandardCharsets.UTF_8)));
        Files.writeString(out.resolve("source_compiler.cspec"), source);
        Files.writeString(out.resolve("context_view.cspec"), modified);
        return (Program)Proxy.newProxyInstance(Program.class.getClassLoader(),
                new Class<?>[]{Program.class}, (proxy, method, values) -> {
                    if (method.getName().equals("getCompilerSpec")) return view;
                    try { return method.invoke(program, values); }
                    catch (InvocationTargetException failure) { throw failure.getCause(); }
                });
    }
}
