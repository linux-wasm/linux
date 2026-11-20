// Raising kernel interrupts from a thread that is not a kernel thread.
//
// wasm_raise_irq only touches shared memory, so it can be called on an
// instance of the kernel that is otherwise unused; none of that
// instance's imports are ever called.

// Returns a promise for a function raise(irq).
export async function irqRaiser(module, memory) {
  const instance = await WebAssembly.instantiate(module, {
    env: { memory },
    host: Object.fromEntries(
      WebAssembly.Module.imports(module)
        .filter((i) => i.module === "host")
        .map((i) => [i.name, () => { throw new Error(`host.${i.name} called on the irq instance`); }]),
    ),
  });
  return (irq) => instance.exports.wasm_raise_irq(irq);
}
