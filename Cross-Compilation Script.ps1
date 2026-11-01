# =============================================================================
#  build.ps1 - compile, link and flash a bare-metal STM32F401RE project
# =============================================================================
#
#  ONE COMMAND:   .\build.ps1
#
#  That finds the sources, compiles them, links them into a .elf, reports how
#  much flash and RAM it uses, and writes it to the board over SWD.
#
#  Useful variations:
#      .\build.ps1 -NoFlash          build only, do not touch the board
#      .\build.ps1 -Release          optimise for size and speed, no debug info
#      .\build.ps1 -Clean            delete previous build output first
#
#  If PowerShell refuses to run this at all ("running scripts is disabled"),
#  unblock it for the current window only:
#      Set-ExecutionPolicy -Scope Process -Bypass
#
#  Requires: arm-none-eabi-gcc on PATH (ARM GNU Toolchain, developer.arm.com)
#            STM32_Programmer_CLI    (bundled with CubeIDE / CubeProgrammer)
# =============================================================================

param(
    # -NoFlash: stop after producing the .elf. Useful when the board is not
    # plugged in, or when you only want to see whether it compiles.
    [switch]$NoFlash,

    # -Release: optimise properly and drop debug symbols. Note this changes
    # timing-by-busy-loop code, which is one reason to use a real timer.
    [switch]$Release,

    # -Clean: remove the previous .o/.elf/.map before building, so nothing
    # stale can survive into the new binary.
    [switch]$Clean,

    # Where the code lives. Defaults match a CubeIDE "Empty" project.
    [string]$SourceDir  = "Src",
    [string]$IncludeDir = "Inc",

    # The build products.
    [string]$Output     = "firmware.elf",
    [string]$BuildDir   = "build"
)

# Stop the whole script on an unhandled error rather than limping onward.
$ErrorActionPreference = "Stop"


# =============================================================================
#  CHIP FLAGS
# =============================================================================
#  These describe the hardware and must be identical for every compile AND for
#  the link. Mismatch them and you get "error: uses VFP register arguments but
#  <file> does not", which is the linker noticing half the binary disagrees
#  about how floats are passed.
#
#    -mcpu=cortex-m4      the CPU core, so GCC emits the right instructions
#    -mthumb              Thumb-2 instruction set - Cortex-M runs nothing else
#    -mfpu=fpv4-sp-d16    the hardware FPU: SINGLE PRECISION ONLY.
#                         'float' runs in hardware; 'double' silently falls
#                         back to software emulation, roughly 10x slower.
#                         Matters a great deal once filtering starts.
#    -mfloat-abi=hard     pass floats in FPU registers rather than integer ones
# =============================================================================

$chip = @(
    "-mcpu=cortex-m4"
    "-mthumb"
    "-mfpu=fpv4-sp-d16"
    "-mfloat-abi=hard"
)


# =============================================================================
#  OPTIMISATION
# =============================================================================
#  -Og  "optimise for debugging": inlines 'static inline' helpers so the
#       readable gpio_high() really does compile to one instruction, while
#       keeping the code steppable. The sensible default here.
#
#  -O0  no optimisation at all. Every helper becomes a real function call.
#       Only worth it when -Og is somehow confusing the debugger.
#
#  -O2  full optimisation, used by -Release. Faster and smaller, but the
#       debugger will jump around because statements get reordered.
#
#  -g3  full debug symbols, including macros, so the debugger can show you
#       what REG(PORT_A, 0x14) expands to.
#
#  -Wall -Wextra  turn on warnings. Read them. On bare metal the compiler
#       catching a wrong-width assignment is far cheaper than finding it
#       with an oscilloscope.
# =============================================================================

if ($Release) {
    $optimise = @("-O2", "-DNDEBUG")
} else {
    $optimise = @("-Og", "-g3", "-DDEBUG")
}

$warnings = @("-Wall", "-Wextra")


# =============================================================================
#  SANITY CHECKS
# =============================================================================
#  Fail early with an explanation, rather than halfway through with a cryptic
#  toolchain message.
# =============================================================================

function Fail($message, $hint) {
    Write-Host ""
    Write-Host "  $message" -ForegroundColor Red
    if ($hint) { Write-Host "  $hint" -ForegroundColor Yellow }
    Write-Host ""
    exit 1
}

# Is the cross-compiler installed? Note this is arm-none-eabi-gcc specifically.
# A toolchain named arm-linux-gnueabihf is a DIFFERENT target - it builds for
# ARM chips running Linux, with an MMU and a kernel underneath. It cannot
# produce code for a Cortex-M.
if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    Fail "arm-none-eabi-gcc is not on PATH." `
         "Install the ARM GNU Toolchain from developer.arm.com, then add its bin folder to PATH."
}

if (-not (Test-Path $SourceDir)) {
    Fail "No '$SourceDir' folder here." `
         "Run this from the project root, or pass -SourceDir <path>."
}


# =============================================================================
#  FIND THE SOURCES
# =============================================================================
#  Three kinds of input go into the binary:
#
#    .c files    - your code. All of them, so adding a new one needs no edit
#                  to this script.
#    .s file     - the startup code: it builds the vector table, sets the
#                  stack pointer, zeroes .bss, copies .data from flash into
#                  RAM, then calls main(). Without it the chip resets into
#                  nothing.
#    .ld file    - the linker script: says flash begins at 0x08000000 and is
#                  512 KB, RAM begins at 0x20000000 and is 96 KB, and the
#                  stack starts at the top of RAM. This is what puts your
#                  vector table at the one address the CPU looks at.
#
#  Headers (.h) are NOT compiled. They are pasted into the .c files by the
#  preprocessor, which is why only the -I path below matters for them.
# =============================================================================

$cFiles = @(Get-ChildItem -Path $SourceDir -Filter *.c -Recurse | ForEach-Object { $_.FullName })
if ($cFiles.Count -eq 0) {
    Fail "No .c files found under '$SourceDir'." $null
}

# The startup file and linker script could be anywhere depending on which
# CubeIDE version generated the project, so search rather than assume.
$startup = Get-ChildItem -Path . -Filter *.s -Recurse -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -notmatch '\\build\\' } |
           Select-Object -First 1

$linker  = Get-ChildItem -Path . -Filter *.ld -Recurse -ErrorAction SilentlyContinue |
           Select-Object -First 1

if (-not $startup) {
    Fail "No startup .s file found." `
         "It comes with the CubeIDE project. Without it there is no vector table and the chip will not boot."
}
if (-not $linker) {
    Fail "No linker script (.ld) found." `
         "It comes with the CubeIDE project. Without it the linker does not know where flash and RAM are."
}


# =============================================================================
#  INCLUDE PATHS
# =============================================================================
#  -I tells the preprocessor where to look for #include "..." files. We add
#  both Inc and Src, so headers can sit in either without this script caring.
# =============================================================================

$includes = @()
if (Test-Path $IncludeDir) { $includes += "-I$IncludeDir" }
$includes += "-I$SourceDir"


# =============================================================================
#  BUILD
# =============================================================================

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host ""
Write-Host "  Building $Output" -ForegroundColor Cyan
Write-Host "  startup: $($startup.Name)   linker: $($linker.Name)   mode: $(if ($Release) {'release'} else {'debug'})" -ForegroundColor DarkGray
Write-Host ""

$objects = @()

# ---- compile each .c file on its own ------------------------------------
# -c means "compile only, do not link yet". Each .c becomes one .o holding
# machine code with the addresses still unresolved.
foreach ($c in $cFiles) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($c)
    $obj  = Join-Path $BuildDir "$name.o"

    Write-Host "    compile  $(Split-Path $c -Leaf)" -ForegroundColor DarkGray
    arm-none-eabi-gcc -c $c -o $obj @chip @optimise @warnings @includes -std=gnu11 `
        -ffunction-sections -fdata-sections
    #   ^ -ffunction-sections puts each function in its own section, so the
    #     linker's --gc-sections below can throw away the ones nothing calls.

    if ($LASTEXITCODE -ne 0) { Fail "Compilation failed." "Fix the FIRST error listed - later ones are usually knock-on effects." }
    $objects += $obj
}

# ---- assemble the startup file -------------------------------------------
# No FPU flags here: it is plain assembly, it passes no floats, and some
# startup files reject the float ABI options.
$startupObj = Join-Path $BuildDir "startup.o"
Write-Host "    assemble $($startup.Name)" -ForegroundColor DarkGray
arm-none-eabi-gcc -c $startup.FullName -o $startupObj -mcpu=cortex-m4 -mthumb
if ($LASTEXITCODE -ne 0) { Fail "Assembling the startup file failed." $null }
$objects += $startupObj

# ---- link -----------------------------------------------------------------
# This is where the placeholder addresses get filled in for real, guided by
# the linker script.
#
#   -T <script>          the memory layout. Mandatory on bare metal.
#   --specs=nosys.specs  stub out printf/file I/O/exit - there is no OS to
#                        provide them. Without this you get undefined
#                        references to _write, _sbrk and friends.
#   --specs=nano.specs   the cut-down C library. Much smaller than full newlib.
#   --gc-sections        actually discard the unused sections.
#   -Map                 write a report of what ended up where and how big it
#                        is. Worth opening when flash starts filling up.
#   -lm                  the maths library. Not needed yet; will be once the
#                        filter chain uses sqrtf and friends.
$elf = Join-Path $BuildDir $Output
Write-Host "    link     $Output" -ForegroundColor DarkGray
arm-none-eabi-gcc @objects -o $elf @chip `
    -T $linker.FullName `
    --specs=nosys.specs --specs=nano.specs `
    -Wl,--gc-sections `
    -Wl,-Map=$(Join-Path $BuildDir 'firmware.map') `
    -lm
if ($LASTEXITCODE -ne 0) {
    Fail "Linking failed." "An 'undefined reference' here usually means a .c file was not found, or a handler name is misspelled."
}


# =============================================================================
#  SIZE REPORT
# =============================================================================
#   text = code + constants, lives in flash          (512 KB available)
#   data = initialised globals: counted TWICE - the values sit in flash and
#          are copied into RAM at boot
#   bss  = zeroed globals, RAM only, costs no flash
#
#   flash used = text + data        RAM used = data + bss
#
#  RAM is the one to watch: 96 KB total, and the stack grows down into
#  whatever is left. Nothing detects a collision at runtime.
# =============================================================================

Write-Host ""
arm-none-eabi-size $elf
Write-Host ""


if ($NoFlash) {
    Write-Host "  Built $elf (not flashed)." -ForegroundColor Green
    Write-Host ""
    exit 0
}


# =============================================================================
#  FLASH
# =============================================================================
#  SWD is ARM's two-wire debug protocol: clock plus bidirectional data. It
#  reaches straight into the chip's bus, so it can write flash, read RAM and
#  halt the core.
#
#  On a Nucleo the ST-Link chip on the top third of the board speaks USB to
#  the PC and SWD to the F401, with the traces already routed - so one cable
#  and nothing to wire up.
#
#  -rst resets the chip afterwards, so the new firmware starts immediately.
# =============================================================================

$prog = (Get-Command STM32_Programmer_CLI -ErrorAction SilentlyContinue).Source

if (-not $prog) {
    # Not on PATH. It ships inside CubeIDE and CubeProgrammer, so look there.
    $search = @("C:\ST", "C:\Program Files\STMicroelectronics", "C:\Program Files (x86)\STMicroelectronics")
    foreach ($dir in $search) {
        if (Test-Path $dir) {
            $hit = Get-ChildItem $dir -Recurse -Filter "STM32_Programmer_CLI.exe" -ErrorAction SilentlyContinue |
                   Select-Object -First 1
            if ($hit) { $prog = $hit.FullName; break }
        }
    }
}

if (-not $prog) {
    Write-Host "  Built $elf, but STM32_Programmer_CLI was not found." -ForegroundColor Yellow
    Write-Host "  Flash from CubeIDE instead, or install STM32CubeProgrammer." -ForegroundColor Yellow
    Write-Host ""
    exit 0
}

Write-Host "  Flashing over SWD" -ForegroundColor Cyan
& $prog -c port=SWD -w $elf -rst

if ($LASTEXITCODE -ne 0) {
    Fail "Flashing failed." "Check the board is plugged in. If it is, the running firmware may have disabled SWD - hold the reset button while flashing."
}

Write-Host ""
Write-Host "  Done." -ForegroundColor Green
Write-Host ""
