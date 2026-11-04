// Create functions at the given addresses (if missing) and write their decompiled C to an output dir.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;

public class DecompAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outDir = args[0];
        new File(outDir).mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            Address a = toAddr(args[i]);
            Function f = getFunctionAt(a);
            if (f == null) {
                Function c = getFunctionContaining(a);
                if (c != null && !c.getEntryPoint().equals(a)) {
                    // split: shrink the containing function's body is complex; just create a new one
                }
                disassemble(a);
                CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                cmd.applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
            }
            if (f == null) { println("FAILED " + args[i]); continue; }
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            String code = r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("// failed: " + r.getErrorMessage());
            try (PrintWriter w = new PrintWriter(new FileWriter(outDir + "/" + args[i] + ".c"))) { w.print(code); }
            println("OK " + args[i] + " " + f.getBody().getNumAddresses());
        }
    }
}
