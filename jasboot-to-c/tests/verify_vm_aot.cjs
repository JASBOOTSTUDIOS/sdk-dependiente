"use strict";
/**
 * Comprueba que la salida en consola del .jbo (VM) coincide con la del ejecutable AOT (jbc-to-c + gcc).
 * Uso (desde la raiz del repo jasboot):
 *   node sdk-dependiente/jasboot-to-c/tests/verify_vm_aot.cjs
 *   node sdk-dependiente/jasboot-to-c/tests/verify_vm_aot.cjs ruta/a/prueba.jasb
 *   node sdk-dependiente/jasboot-to-c/tests/verify_vm_aot.cjs a.jasb b.jasb
 *
 * Sin argumentos: ejecuta el lote por defecto (lista defaultSuite; 105 usa 105_atm_interactivo.stdin si existe).
 * Opcional: JASBOOT_JBC_TO_C=ruta/al/jbc-to-c.exe (o .exe junto a tests/) para forzar el transpilador AOT.
 *
 * Requisitos: jbc.exe en sdk-dependiente/jas-compiler-c/bin, VM en jasboot-ir/bin, jbc-to-c junto al script o en PATH.
 */
const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const workspaceRoot = path.resolve(__dirname, "../../..");
const childEnv = { ...process.env, JASBOOT_REPO_ROOT: workspaceRoot };
/* La VM escribe trazas extra a stdout con JASBOOT_DEBUG; rompe la comparación línea a línea. */
delete childEnv.JASBOOT_DEBUG;

const defaultSuite = [
  path.join(__dirname, "lenguaje", "102_parity_vm_aot.jasb"),
  path.join(__dirname, "lenguaje", "103_aot_lista_dividir.jasb"),
  path.join(__dirname, "lenguaje", "104_aot_builtins_extra.jasb"),
  path.join(__dirname, "lenguaje", "105_atm_interactivo.jasb"),
  path.join(__dirname, "lenguaje", "106_aot_shift_alias.jasb"),
  path.join(__dirname, "lenguaje", "107_aot_archivos_fs.jasb"),
  path.join(__dirname, "lenguaje", "108_aot_fs_binario.jasb"),
  path.join(__dirname, "lenguaje", "109_aot_fs_extra.jasb"),
  path.join(__dirname, "lenguaje", "110_aot_json.jasb"),
  path.join(__dirname, "lenguaje", "111_aot_json_literal_init.jasb"),
  path.join(__dirname, "lenguaje", "112_aot_usar_global.jasb"),
  path.join(__dirname, "lenguaje", "113_aot_usar_registro.jasb"),
  path.join(__dirname, "lenguaje", "114_aot_nested_member_assign.jasb"),
  path.join(__dirname, "lenguaje", "115_aot_lista_ruta_mapa.jasb"),
  path.join(__dirname, "lenguaje", "116_aot_index_miembro_mapa.jasb"),
  path.join(__dirname, "lenguaje", "117_aot_jmn_asociaciones.jasb"),
  path.join(__dirname, "lenguaje", "119_aot_jmn_stmt_calls.jasb"),
  path.join(__dirname, "lenguaje", "118_aot_vec_mat.jasb"),
  path.join(__dirname, "analitica-neuronal", "200_aot_analitica_math.jasb"),
  path.join(__dirname, "analitica-neuronal", "201_aot_analitica_normalizacion.jasb"),
  path.join(__dirname, "analitica-neuronal", "202_aot_analitica_metricas_regresion.jasb"),
  path.join(__dirname, "analitica-neuronal", "203_aot_nativo_mlp_entrenar.jasb"),
  path.join(__dirname, "lenguaje", "pesado_bin_json_lab", "pl_main.jasb"),
  path.join(workspaceRoot, "tests", "test_gestion_secuencias_completo.jasb"),
];

function findJbc() {
  if (process.env.JASBOOT_JBC) {
    const p = path.isAbsolute(process.env.JASBOOT_JBC)
      ? process.env.JASBOOT_JBC
      : path.join(workspaceRoot, process.env.JASBOOT_JBC);
    if (fs.existsSync(p)) return p;
  }
  const rels = [
    ["sdk-dependiente", "jas-compiler-c", "bin", "jbc.exe"],
    ["sdk-dependiente", "jas-compiler-c", "bin", "jbc-next.exe"],
    ["sdk", "jas-compiler-c", "bin", "jbc.exe"],
  ];
  for (const parts of rels) {
    const p = path.join(workspaceRoot, ...parts);
    if (fs.existsSync(p)) return p;
  }
  return null;
}

function findVm() {
  const rels = [
    ["sdk-dependiente", "jasboot-ir", "bin", "jasboot-ir-vm.exe"],
    ["sdk-dependiente", "jasboot-ir", "bin", "jasboot-ir-vm-net.exe"],
    ["sdk", "jasboot-ir", "bin", "jasboot-ir-vm.exe"],
  ];
  for (const parts of rels) {
    const p = path.join(workspaceRoot, ...parts);
    if (fs.existsSync(p)) return p;
  }
  return null;
}

function findJbcToC() {
  if (process.env.JASBOOT_JBC_TO_C) {
    const p = path.isAbsolute(process.env.JASBOOT_JBC_TO_C)
      ? process.env.JASBOOT_JBC_TO_C
      : path.join(workspaceRoot, process.env.JASBOOT_JBC_TO_C);
    if (fs.existsSync(p)) return p;
  }
  const local = path.join(
    __dirname,
    "..",
    process.platform === "win32" ? "jbc-to-c.exe" : "jbc-to-c",
  );
  if (fs.existsSync(local)) return local;
  return null;
}

/** Quita códigos ANSI (la VM puede emitir p. ej. limpiar pantalla al leer stdin). */
function stripAnsi(s) {
  if (!s) return "";
  return s.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "").replace(/\x1b[@-_]/g, "");
}

function normalizeOut(s) {
  if (!s) return "";
  return stripAnsi(s)
    .replace(/\f/g, "")
    .replace(/\r\n/g, "\n")
    .replace(/\r/g, "\n")
    .replace(/\s+$/g, "");
}

/** Una linea logica equivalente VM <-> AOT (diferencias historicas de formato). */
function canonParityLine(line) {
  let t = line.trim();
  if (t === "verdadero") t = "1";
  else if (t === "falso") t = "0";
  else if (t.length >= 2 && t[0] === '"' && t[t.length - 1] === '"')
    t = t.slice(1, -1).replace(/\\"/g, '"');
  if (t.startsWith("\\u000a")) t = t.slice(6);
  /* VM: prompt `> "texto"` en AOT vs `> texto` en VM (imprimir_sin_salto + imprimir). */
  const m = t.match(/^> \"(.*)\"$/);
  if (m) t = "> " + m[1];
  const cq = t.match(/^([^"]+?: )\"(.*)\"$/);
  if (cq) t = cq[1] + cq[2];
  /* VM suele imprimir flotantes con decimales; AOT puede usar %g ("2" vs "2.0000"). */
  if (/^-?\d+(\.\d+)?([eE][+-]?\d+)?$/.test(t)) {
    const x = parseFloat(t);
    if (!Number.isNaN(x)) {
      /* Redondeo fino: float IEEE en AOT vs VM formateada (p. ej. 0.9 vs 0.899999976158142). */
      if (Number.isFinite(x) && Math.floor(x) !== x)
        return String(Math.round(x * 1e6) / 1e6);
      return String(x);
    }
  }
  return t;
}

function parityEquals(vmText, aotText) {
  const a = normalizeOut(vmText).split("\n").filter((x) => x.length > 0);
  const b = normalizeOut(aotText).split("\n").filter((x) => x.length > 0);
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (canonParityLine(a[i]) !== canonParityLine(b[i])) return false;
  }
  return true;
}

/** Si existe <mismoNombre>.stdin junto al .jasb, se inyecta a VM y al exe AOT. */
function resolveStdinForJasb(jasbPath) {
  const p = jasbPath.replace(/\.jasb$/i, ".stdin");
  if (!fs.existsSync(p)) return null;
  return fs.readFileSync(p, "utf8");
}

function resolveExeFromJasb(jasbPath) {
  const dir = path.dirname(jasbPath);
  const stem = path.basename(jasbPath, ".jasb");
  const name = process.platform === "win32" ? stem + ".exe" : stem;
  return path.join(dir, name);
}

/** Borra artefactos .jmn fijos usados por pruebas de paridad (misma ruta en VM y AOT). */
function unlinkSilent(p) {
  try {
    if (fs.existsSync(p)) fs.unlinkSync(p);
  } catch (_) {
    /* ignorar */
  }
}

/**
 * Los programas abren .jmn con jmn_abrir_escritura (carga si existe) y recordar/asociar
 * suman fuerza en la arista. El verificador ejecuta VM y luego el exe AOT: sin limpiar
 * entre ambos, el AOT re-aprende sobre el grafo ya persistido por la VM y la salida difiere.
 */
function resetJmnParitySidecars() {
  const lenguajeDir = path.join(
    workspaceRoot,
    "sdk-dependiente",
    "jasboot-to-c",
    "tests",
    "lenguaje",
  );
  if (fs.existsSync(lenguajeDir)) {
    for (const name of fs.readdirSync(lenguajeDir)) {
      if (name.endsWith("_mem.jmn")) {
        unlinkSilent(path.join(lenguajeDir, name));
      }
    }
  }
  unlinkSilent(path.join(workspaceRoot, "test_gestion_fresca.jmn"));
}

/**
 * @returns {{ ok: boolean, label: string, err?: string, outVm?: string, outAot?: string }}
 */
function runOne(jasbPath, jbc, vm, jbcToC) {
  const label = path.relative(workspaceRoot, jasbPath) || jasbPath;

  if (!fs.existsSync(jasbPath)) {
    return { ok: false, label, err: "No existe: " + jasbPath };
  }

  const compileVm = spawnSync(jbc, [jasbPath], {
    cwd: workspaceRoot,
    env: childEnv,
    encoding: "utf8",
    windowsHide: true,
  });
  if (compileVm.status !== 0) {
    return {
      ok: false,
      label,
      err:
        "Fallo compilacion jbc:\n" +
        (compileVm.stderr || compileVm.stdout || ""),
    };
  }

  const jboPath = jasbPath.replace(/\.jasb$/i, ".jbo");
  if (!fs.existsSync(jboPath)) {
    return { ok: false, label, err: "No se genero el .jbo: " + jboPath };
  }

  const stdinData = resolveStdinForJasb(jasbPath);
  const runOpts = {
    cwd: workspaceRoot,
    env: childEnv,
    encoding: "utf8",
    windowsHide: true,
  };
  if (stdinData !== null) runOpts.input = stdinData;

  resetJmnParitySidecars();
  const vmRun = spawnSync(vm, [jboPath], runOpts);
  if (vmRun.status !== 0) {
    return {
      ok: false,
      label,
      err: "Fallo VM: " + (vmRun.stderr || vmRun.stdout || ""),
    };
  }

  resetJmnParitySidecars();
  const compileAot = spawnSync(jbcToC, [jasbPath], {
    cwd: workspaceRoot,
    env: childEnv,
    encoding: "utf8",
    windowsHide: true,
  });
  if (compileAot.status !== 0) {
    return {
      ok: false,
      label,
      err:
        "Fallo jbc-to-c (compilar AOT): " +
        (compileAot.stderr || compileAot.stdout || ""),
    };
  }

  const exePath = resolveExeFromJasb(jasbPath);
  if (!fs.existsSync(exePath)) {
    return {
      ok: false,
      label,
      err: "No se encontro el ejecutable AOT (tras jbc-to-c): " + exePath,
    };
  }

  const exeRun = spawnSync(exePath, [], runOpts);
  if (exeRun.status !== 0) {
    return {
      ok: false,
      label,
      err: "Fallo ejecutable AOT: " + (exeRun.stderr || exeRun.stdout || ""),
    };
  }

  const outVm = normalizeOut(vmRun.stdout);
  const outAot = normalizeOut(exeRun.stdout);

  if (parityEquals(outVm, outAot)) {
    return { ok: true, label, outVm };
  }
  return {
    ok: false,
    label,
    err: "La salida de la VM y del AOT no coinciden.",
    outVm,
    outAot,
  };
}

function main() {
  const jbc = findJbc();
  const vm = findVm();
  const jbcToC = findJbcToC();

  if (!jbc) {
    console.error("No se encuentra jbc.exe (variable JASBOOT_JBC o bin predefinido).");
    process.exit(1);
  }
  if (!vm) {
    console.error("No se encuentra jasboot-ir-vm.exe.");
    process.exit(1);
  }
  if (!jbcToC) {
    console.error(
      "No se encuentra jbc-to-c junto a tests/ (compile jasboot-to-c primero).",
    );
    process.exit(1);
  }

  const args = process.argv.slice(2).filter((a) => a.length > 0);
  const jasbList =
    args.length > 0
      ? args.map((a) => path.resolve(a))
      : defaultSuite.map((p) => path.resolve(p));

  let failed = 0;
  for (const jasbPath of jasbList) {
    const r = runOne(jasbPath, jbc, vm, jbcToC);
    if (r.ok) {
      console.log("OK:", r.label);
      console.log("---");
      console.log((r.outVm || "").trimEnd());
      console.log("");
    } else {
      console.error("ERROR:", r.label);
      console.error(r.err);
      if (r.outVm != null && r.outAot != null) {
        console.error("=== VM (stdout) ===\n" + r.outVm);
        console.error("\n=== AOT (stdout) ===\n" + r.outAot);
      }
      failed++;
    }
  }

  if (failed > 0) {
    console.error(`Fallaron ${failed} de ${jasbList.length} pruebas.`);
    process.exit(1);
  }
  console.log(
    `Listo: ${jasbList.length} prueba(s) — salida VM y AOT coinciden (tras normalizar texto/bool si aplica).`,
  );
  process.exit(0);
}

main();
