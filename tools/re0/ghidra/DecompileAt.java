import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    private static final int DECOMPILE_TIMEOUT_S = 120;

    @Override
    protected void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (String arg : getScriptArgs()) {
            long offset = Long.decode(arg);
            Function fn = getFunctionAt(toAddr(offset));
            if (fn == null) {
                fn = createFunction(toAddr(offset), null);
            }
            DecompileResults res = decomp.decompileFunction(fn, DECOMPILE_TIMEOUT_S, monitor);
            println("== " + arg + " " + fn.getName());
            if (res.decompileCompleted()) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("decompile failed: " + res.getErrorMessage());
            }
        }
    }
}
