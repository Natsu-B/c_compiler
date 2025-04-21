#!/usr/bin/env -S cargo +nightly -q -Zscript

---
[package]
name = "test"
version = "0.1.0"
edition = "2024"

[dependencies]
clap = { version = "4.5.37", features = ["derive"] }
tokio = { version = "1", features = ["full"] }
futures = { version = "0.3.31" }
tempfile = { version = "3.19.1"}
eframe = { version = "0.31.1" }
---

// --------------------------------------------------------------------
// fast test runner written in rust
// --------------------------------------------------------------------
#![feature(exit_status_error)]
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")] // hide console window on Windows in release

extern crate clap;
extern crate eframe;
extern crate futures;
extern crate tempfile;
extern crate tokio;
use clap::Parser;
use core::panic;
use eframe::egui::{self, Layout};
use futures::stream::FuturesUnordered;
use futures::{StreamExt, future};
use std::{default, env};
use std::{
    io::Write,
    process::Stdio,
    sync::{Arc, Mutex},
    vec,
};
use tempfile::NamedTempFile;
use tokio::process::Command;
use tokio::sync::RwLock;
use tokio::time::{Duration, timeout};

#[derive(Clone, Debug)]
struct Std {
    stdout: Option<String>,
    stderr: Option<String>,
    exit_code: Option<Option<i32>>,
}

#[derive(Clone, Debug)]
struct TestResult {
    gcc: Option<Std>,
    gcc_asm: Option<Std>,
    my_compiler: Option<Std>,
    gcc_result: Option<Std>,
    my_compiler_result: Option<Std>,
}

#[derive(Clone)]
struct Test {
    test_name: usize,
    assert_type: Assert,
    code: &'static str,
    is_success: bool,
    may_be_result: Option<TestResult>,
}

#[derive(PartialEq, Clone, Copy, Debug)]
enum Assert {
    AssertSimple,
    AssertWithOuterCode,
    AssertPrintCheck,
    AssertNegative,
}

/// fast test runner written in rust
#[derive(Parser, Debug)]
#[command(version, about, long_about = None)]
struct Args {
    /// requires gnome-terminal
    #[arg(short, long)]
    show: bool,

    /// test all
    #[arg(short, long)]
    all: bool,

    /// test compiler
    #[arg(short, long)]
    compiler_test: bool,

    /// test preprocessor
    #[arg(short, long)]
    preprocessor_test: bool,
}

async fn _run_async(
    duration: Duration,
    mut test_result: TestResult,
    i: usize,
    assert_type: Assert,
    code: &str,
) -> Result<(TestResult, bool), TestResult> {
    let mut tmp = NamedTempFile::new().unwrap();
    if assert_type == Assert::AssertWithOuterCode {
        #[allow(unused_must_use)]
        let _ = write!(
            tmp,
            "#include \"{}/test/gcc.h\"\n {}",
            env::current_dir().unwrap().display(),
            code
        );
    } else {
        write!(tmp, "{}", code).unwrap();
    }
    let path = tmp.path().to_str().unwrap();

    // gccを起動
    let compile_result = timeout(
        duration,
        if assert_type == Assert::AssertWithOuterCode {
            tokio::process::Command::new("gcc")
                .arg("-xc")
                .arg("-o")
                .arg(format!("./out/gcc{}", i))
                .arg("./test/alloc2.c")
                .arg("./test/fibonacci.c")
                .arg("./test/include_test.c")
                .arg("./test/print_u64.c")
                .arg(path)
                .stdout(Stdio::inherit())
                .stderr(Stdio::inherit())
                .output()
        } else {
            tokio::process::Command::new("gcc")
                .arg("-xc")
                .arg("-o")
                .arg(format!("./out/gcc{}", i))
                .arg(path)
                .stdout(Stdio::inherit())
                .stderr(Stdio::inherit())
                .output()
        },
    )
    .await;
    match compile_result {
        Ok(Ok(status)) => {
            test_result.gcc = Some(Std {
                stdout: Some(String::from_utf8_lossy(&status.stdout).to_string()),
                stderr: Some(String::from_utf8_lossy(&status.stderr).to_string()),
                exit_code: Some(status.status.code()), // Noneだったらsegmentation faultとか
            });
            if !status.status.success() {
                println!("gcc failed!!! (task {}) {}", i, status.status);
                println!("stdout{}\nstderr{}\n",test_result.gcc.clone().unwrap().stdout.unwrap(), test_result.gcc.clone().unwrap().stderr.unwrap());
                return Err(test_result);
            }
        }
        Ok(Err(e)) => panic!("process error: {}", e),
        Err(_) => {
            test_result.gcc = Some(Std {
                stdout: None,
                stderr: None,
                exit_code: None,
            });
            println!("gcc panicked!!! (task {})", i);
            return Err(test_result);
        }
    }

    // コンパイラ起動
    let compile_result = timeout(
        duration,
        tokio::process::Command::new("./main")
            .arg("-o")
            .arg(format!("./out/{}.s", i))
            .arg("-I")
            .arg(format!("{}", code))
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output(),
    )
    .await;
    match compile_result {
        Ok(Ok(status)) => {
            test_result.my_compiler = Some(Std {
                stdout: Some(String::from_utf8_lossy(&status.stdout).to_string()),
                stderr: Some(String::from_utf8_lossy(&status.stderr).to_string()),
                exit_code: Some(status.status.code()), // Noneだったらsegmentation faultとか
            });
            if !status.status.success() {
                println!("my c compiler failed!!! (task {})", i);
                return Err(test_result);
            }
        }
        Ok(Err(e)) => panic!("process error: {}", e),
        Err(_) => {
            test_result.my_compiler = Some(Std {
                stdout: None,
                stderr: None,
                exit_code: None,
            });
            println!("my c compiler panicked!!! (task {})", i);
            return Err(test_result);
        }
    }
    // コンパイラが出力したアセンブリをコンパイルする
    let compile_result = timeout(
        duration,
        if assert_type == Assert::AssertWithOuterCode {
            tokio::process::Command::new("gcc")
                .arg("-o")
                .arg(format!("./out/{}", i))
                .arg(format!("./out/{}.s", i))
                .arg("./test/alloc2.o")
                .arg("./test/fibonacci.o")
                .arg("./test/include_test.o")
                .arg("./test/print_u64.o")
                .stdout(Stdio::piped())
                .stderr(Stdio::piped())
                .output()
        } else {
            tokio::process::Command::new("gcc")
                .arg("-o")
                .arg(format!("./out/{}", i))
                .arg(format!("./out/{}.s", i))
                .stdout(Stdio::piped())
                .stderr(Stdio::piped())
                .output()
        },
    )
    .await;
    match compile_result {
        Ok(Ok(status)) => {
            test_result.gcc_asm = Some(Std {
                stdout: Some(String::from_utf8_lossy(&status.stdout).to_string()),
                stderr: Some(String::from_utf8_lossy(&status.stderr).to_string()),
                exit_code: Some(status.status.code()), // Noneだったらsegmentation faultとか
            });
            if !status.status.success() {
                println!("gcc asm failed!!! (task {})", i);
                return Err(test_result);
            }
        }
        Ok(Err(e)) => panic!("process error: {}", e),
        Err(_) => {
            test_result.gcc_asm = Some(Std {
                stdout: None,
                stderr: None,
                exit_code: None,
            });
            println!("gcc asm panicked!!! (task {})", i);
            return Err(test_result);
        }
    }
    // ここまでで自身のプログラムとgccでのプログラムの両方が揃った
    let compile_result = timeout(
        duration,
        tokio::process::Command::new(format!("./out/{}", i))
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output(),
    )
    .await;
    let my_compiler_result = match compile_result {
        Ok(Ok(status)) => {
            test_result.my_compiler_result = Some(Std {
                stdout: Some(String::from_utf8_lossy(&status.stdout).to_string()),
                stderr: Some(String::from_utf8_lossy(&status.stderr).to_string()),
                exit_code: Some(status.status.code()), // Noneだったらsegmentation faultとか
            });
        }
        Ok(Err(e)) => panic!("process{} error: {}", i, e),
        Err(_) => {
            test_result.my_compiler_result = Some(Std {
                stdout: None,
                stderr: None,
                exit_code: None,
            });
            return Err(test_result);
        }
    };
    let compile_result = timeout(
        duration,
        tokio::process::Command::new(format!("./out/gcc{}", i))
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output(),
    )
    .await;
    let gcc_result = match compile_result {
        Ok(Ok(status)) => {
            test_result.gcc_result = Some(Std {
                stdout: Some(String::from_utf8_lossy(&status.stdout).to_string()),
                stderr: Some(String::from_utf8_lossy(&status.stderr).to_string()),
                exit_code: Some(status.status.code()), // Noneだったらsegmentation faultとか
            });
        }
        Ok(Err(e)) => panic!("process error: {}", e),
        Err(_) => {
            test_result.gcc_result = Some(Std {
                stdout: None,
                stderr: None,
                exit_code: None,
            });
            return Err(test_result);
        }
    };
    return Ok((
        test_result.clone(),
        if assert_type != Assert::AssertPrintCheck {
            my_compiler_result == gcc_result
        } else {
            my_compiler_result == gcc_result
                && test_result.gcc_result.unwrap().stdout
                    == test_result.my_compiler_result.unwrap().stdout
        },
    ));
}

async fn run_async(
    tests: Vec<(Assert, &'static str)>,
    result: Arc<RwLock<Vec<(String, Vec<Test>)>>>,
) {
    let path = std::env::current_dir().unwrap();
    println!("starting dir: {}", path.display());
    let duration = Duration::from_secs(5);
    let mut handles = FuturesUnordered::new();
    let test_result_vec = Vec::new();
    let test_name = "compiler".to_string();
    let count = {
        let result_clone = Arc::clone(&result);
        let mut lock = result_clone.write().await;
        lock.push((test_name, test_result_vec));
        lock.len() - 1
    };

    for (i, (assert_type, code)) in tests.into_iter().enumerate() {
        println!("{}", i);
        let result_clone = Arc::clone(&result);

        handles.push(async move {
            // テストコードをファイルに書き込み
            let test_data = Test {
                assert_type,
                code,
                may_be_result: None,
                test_name: i,
                is_success: false,
            };
            let index = {
                let mut lock = result_clone.write().await;
                let test = lock.get_mut(count).unwrap();
                test.1.push(test_data);
                test.1.len() - 1
            }; // RwLock開放
            let test_result = TestResult {
                gcc: None,
                gcc_asm: None,
                my_compiler: None,
                gcc_result: None,
                my_compiler_result: None,
            };

            let result = _run_async(duration, test_result, i, assert_type, code).await;
            let result_success;
            let test_result;
            if let Err(success) = result {
                result_success = false;
                test_result = success;
            } else {
                result_success = result.clone().unwrap().1;
                test_result = result.unwrap().0;
            }

            {
                let mut lock = result_clone.write().await;
                let test = lock.get_mut(count).unwrap().1.get_mut(index).unwrap();
                test.may_be_result = Some(test_result);
                test.is_success = result_success;
            }
            result_success
        });
    }

    // 全タスクを待つ
    let mut failures = 0;
    while let Some(result) = handles.next().await {
        if !result {
            failures += 1;
        }
    }

    if failures == 0 {
        println!("All tests passed.");
    } else {
        println!("{} tests failed.", failures);
    }
}

// run test
async fn run_tests(
    test_compiler: bool,
    test_preprocess: bool,
    tests: Arc<RwLock<Vec<(String, Vec<Test>)>>>,
) {
    if test_compiler {
        println!("start tests...");
        let compiler_tests = vec![
            (Assert::AssertSimple, "int main() {return 0;}"),
            (Assert::AssertSimple, "int main() {return 42; }"),
            (Assert::AssertSimple, "int main ( ) {return 10-1;}"),
            (Assert::AssertSimple, "int main () {return 9+8+5-4;}"),
            (Assert::AssertSimple, "int main () {return 10 -9 + 5;}"),
            (
                Assert::AssertSimple,
                "int main () {return 10 / (4+ (3 - 2)) ;}",
            ),
            (Assert::AssertSimple, "int main(){return 1* 8 * (4 / 2);}"),
            (Assert::AssertSimple, "int main(){return -1 + 2;}"),
            (Assert::AssertSimple, "int main(){return +9+(-1)+2;}"),
            (Assert::AssertSimple, "int main(){return -(+1-2);}"),
            (Assert::AssertSimple, "int main(){return 0 == 1;}"),
            (Assert::AssertSimple, "int main(){return 0 ==0;}"),
            (Assert::AssertSimple, "int main() {return 0 != 0;}"),
            (Assert::AssertSimple, "int main() {return 0 != 1;}"),
            (Assert::AssertSimple, "int main() {return 1 < 1;}"),
            (Assert::AssertSimple, "int main() {return 0 < 1;}"),
            (Assert::AssertSimple, "int main() {return 10 <= 1;}"),
            (Assert::AssertSimple, "int main() {return 1 <= 1;}"),
            (Assert::AssertSimple, "int main(){ return   0 > 1;}"),
            (Assert::AssertSimple, "int main() { return 0 <= 1;}"),
            (Assert::AssertSimple, "int main() {return 1 > 1;}"),
            (Assert::AssertSimple, "int main() {return 1 > 0;}"),
            (Assert::AssertSimple, "int main() {return 0 >= 1;}"),
            (Assert::AssertSimple, "int main() {return 1 >= 1;}"),
            (
                Assert::AssertSimple,
                "int main() {int a= 5; int b = -1;return a+b;}",
            ),
            (Assert::AssertSimple, "int main() {return 10 >= 1 ;}"),
            (
                Assert::AssertSimple,
                "int main() {int a=5; int b=2; return a*b/2+5;}",
            ),
            (Assert::AssertSimple, "int main () {int a = 1;return 1;}"),
            (
                Assert::AssertSimple,
                "int main() {int FOGE = 10; int HUGA = 5; int PIYO = FOGE / HUGA; return PIYO + 1;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int foge = 1; int huga = 2; int piyo = 1; return foge * huga - piyo;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int a=5; int b=2; int c=a*b/2+5;return c;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int a = 2; int b = 3; int a_b = a +b; return a_b; a * 2;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int returne = 1; return returne + 1;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {if (0) return 10; else return 6;}",
            ),
            (
                Assert::AssertSimple,
                "int main(){if(1) return 10; return 6;}",
            ),
            (
                Assert::AssertSimple,
                "int main(){int j=0;for(int i=0;i < 10; i = i+1) j = j+i; return j;}",
            ),
            (
                Assert::AssertSimple,
                "int main(){int i=0;int j=1;while(i != 10) i = i+j+1; return i;}",
            ),
            (Assert::AssertSimple, "int main(){{} return 10;}"),
            (
                Assert::AssertSimple,
                "int main(){int i=0; int j=1; while(i != 10) {j = j+i; i = i+1;} return j;}",
            ),
            (
                Assert::AssertSimple,
                "int return_val() {return 5;}int main() {return return_val();}",
            ),
            (
                Assert::AssertSimple,
                "int fibonacci(int a, int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); y = fibonacci(x, y); } return y; }",
            ),
            (
                Assert::AssertWithOuterCode,
                "int main() {int i = func();return i;}",
            ),
            (
                Assert::AssertWithOuterCode,
                "int main() {int x = 0;int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)){ return 1;} y = fibonacci(x, y); if (y != func1(y)) {return 1;}} return 0; }",
            ),
            (
                Assert::AssertWithOuterCode,
                "int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)) return 0; y = fibonacci(x, y); if (y != func1(y)) return 1; } return 0; }",
            ),
            (
                Assert::AssertSimple,
                "int main() {int i = 4;int *x = &i; return *x;}",
            ),
            (Assert::AssertSimple, "int main() {int K = 6; return *&K;}"),
            (
                Assert::AssertWithOuterCode,
                "int main() {int x = 0; for(int i= 0; i < 10 ; i =i+1) { x = x+ i; if (x !=func1(x)) return 1; }return 5;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int x = 3; int *y = &x; *y = 0; return x;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int x = 0; int *y = &x; int **z = &y;*y = 2; **z = 3; return x;}",
            ),
            (
                Assert::AssertWithOuterCode,
                "int main() {int *p; alloc(&p, 1, 3); int *q = p+1; return *q;}",
            ),
            (
                Assert::AssertWithOuterCode,
                "int main() {int *p; alloc(&p, 1, 3); int *q = p + 1; q = q-1; return *q;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int x; long z; if (sizeof(x) != 4) return 1; int* y; if (sizeof(y) != 8) return 1; if (sizeof(*y) != 4) return 1; if (sizeof(x + 1) != 4) return 1; if (sizeof(z) != 8) return 1; if (sizeof(sizeof(1)) != 8)return 1;  return 0;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int x[2]; int y; long z; *(x+1) = 1; *x = 2; y = 5; return (y - *(x+1)) / *x;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int x[2]; int y = 1; x[0] = y; x[1] = y + 1; return x[1] + x[0];}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int i = 0; {int i = 1; return i;}}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int i = 0; {int x = 1;} return i;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int i = 0; {int i = 1; } return i;}",
            ),
            (
                Assert::AssertSimple,
                "int i; int main() {int i = 5; return i;}",
            ),
            (
                Assert::AssertSimple,
                "int i; int main() {int j = 5; return i;}",
            ),
            (Assert::AssertSimple, "int i; int main() {i= 5; return i;}"),
            (
                Assert::AssertSimple,
                "int main() {long i = 0; i = i+ 1; return i;}",
            ),
            (Assert::AssertSimple, "int main() {char i; i= 3; return i;}"),
            (
                Assert::AssertSimple,
                "int main() {char i[4]; i[3] = 10; i[2] = 0; int j = 2; return i[2] + i[3] +j;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {char i; char a = 100; char b = 3; char c = 4; i = a * b /c; return i;}",
            ),
            (
                Assert::AssertSimple,
                "int hoge; int main() {int x = 2; x = hoge; return x;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {char *i = \"abc\"; return i[0];}",
            ),
            (
                Assert::AssertSimple,
                "int main() {char*i = \"abc\"; return i[2];}",
            ),
            (
                Assert::AssertSimple,
                "int i; int main() {int i = 1; {int i = 2; {int i = 3; return i;}}}",
            ),
            (
                Assert::AssertSimple,
                "int i; int main() {int i = 1; {int i = 2; {int i = 3;} return i;}}",
            ),
            (
                Assert::AssertSimple,
                "int i; int main() {int i = 1; {int i = 2; {int i = 3;}} return i;}",
            ),
            (
                Assert::AssertPrintCheck,
                "int main() {char* hoge = \"%s\\n\"; char* tmp = \"Hello World!!!\"; printf(hoge, tmp); return 0;}",
            ),
            (
                Assert::AssertPrintCheck,
                "int main() {char* tmp = \"Hello World!!!\"; printf(\"%s\\n\", tmp); return 0;}",
            ),
            (
                Assert::AssertSimple,
                "int main() {int hoge = 10; hoge /* = 0*/
; return hoge; }",
            ),
            (
                Assert::AssertSimple,
                "int main() {int hoge = 15; // hoge = 10;\nreturn hoge;}",
            ),
            (Assert::AssertSimple, "int x; int y; int main() {return x;}"),
        ];

        println!("start compiler tests");
        run_async(compiler_tests, tests).await;
    }
}

#[tokio::main]
async fn main() -> eframe::Result {
    let args = Args::parse();

    let _ = std::process::Command::new("make")
        .spawn()
        .expect("failed to compile");
    std::fs::create_dir_all("out").unwrap();

    // create gui windows
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_title("compiler tester".to_string()),
        ..Default::default()
    };
    eframe::run_native(
        "compiler tester",
        options,
        Box::new(|cc| {
            // This gives us image support:
            Ok(Box::<Tester>::default())
        }),
    )
}

// -----------------------------------------------------------------------
// egui
// -----------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq)]
enum StdioSwitch {
    StdOut,
    StdErr,
}

#[derive(Clone, Copy, PartialEq)]
enum TabSwitcher {
    Reserved,
    MyCompiler,
    Gcc,
}

struct Tester {
    test_preprocess: bool,
    test_compiler: bool,
    in_tests: bool,
    tests: Arc<RwLock<Vec<(String, Vec<Test>)>>>,
    counter: usize,
    tests_clone: Vec<(String, Vec<Test>)>,
    show_result: ((bool, usize), (bool, TabSwitcher), StdioSwitch),
}

impl Default for Tester {
    fn default() -> Self {
        Self {
            test_preprocess: false,
            test_compiler: false,
            in_tests: false,
            tests: Arc::new(RwLock::new(Vec::new())),
            counter: 0,
            tests_clone: Vec::new(),
            show_result: (
                (false, 0),
                (false, TabSwitcher::Reserved),
                StdioSwitch::StdOut,
            ),
        }
    }
}

impl eframe::App for Tester {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("compiler tests");
            egui::ScrollArea::vertical().show(ui, |ui| {
                self.counter += 1;
                if self.counter == 10 {
                    self.counter = 0;
                    let lock = futures::executor::block_on(self.tests.read());
                    self.tests_clone = lock.clone();
                }
                for (test_name, results) in self.tests_clone.clone().into_iter() {
                    ui.heading(test_name);
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        for result in results {
                            if ui
                                .label(format!(
                                    "test case {}: {} (assert type: {:?})",
                                    result.test_name,
                                    if result.is_success {
                                        "success"
                                    } else {
                                        "failed"
                                    },
                                    result.assert_type
                                ))
                                .clicked()
                            {
                                self.show_result = (
                                    (true, result.test_name),
                                    (false, TabSwitcher::Reserved),
                                    StdioSwitch::StdOut,
                                );
                            }
                        }
                    });
                }
            });
        });
        egui::SidePanel::right("right panel")
            .exact_width(150.0)
            .show(ctx, |ui| {
                ui.vertical_centered(|ui| {
                    ui.heading("test type");
                });
                egui::ScrollArea::vertical().show(ui, |ui| {
                    ui.checkbox(&mut self.test_preprocess, "preprocess");
                    ui.checkbox(&mut self.test_compiler, "compiler");
                });
                ui.with_layout(Layout::bottom_up(egui::Align::Center), |ui| {
                    if ui.button("run").clicked() && !self.in_tests {
                        self.in_tests = true;
                        let test_compiler = self.test_compiler;
                        let test_preprocess = self.test_preprocess;
                        let tests = self.tests.clone();
                        let test_flag = Arc::new(Mutex::new(self.in_tests));
                        let ctx_clone = ctx.clone();
                        tokio::spawn(async move {
                            run_tests(test_compiler, test_preprocess, tests).await;
                            let mut flag = test_flag.lock().unwrap();
                            *flag = false;
                            ctx_clone.request_repaint();
                        });
                    }
                })
            });
        if self.show_result.0.0 {
            let test_result = self
                .tests_clone
                .get(0)
                .unwrap()
                .1
                .get(self.show_result.0.1)
                .unwrap();
            egui::Window::new(format!(
                "test case {}: {}",
                self.show_result.0.1,
                if test_result.is_success {
                    "success"
                } else {
                    "failed"
                }
            ))
            .open(&mut self.show_result.0.0)
            .show(ctx, |ui| {
                ui.label(format!("{}", test_result.code));
                ui.label(format!("{:?}", test_result.assert_type));
                if let Some(test_result) = &test_result.may_be_result {
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        ui.separator();
                        ui.label("my compiler compile result");
                        if let Some(test_result) = &test_result.my_compiler {
                            ui.label(format!(
                                "exit code: {}",
                                test_result
                                    .exit_code
                                    .map_or("maybe time out".to_string(), |tmp| tmp.map_or(
                                        "process terminated by a signal".to_string(),
                                        |tmp| tmp.to_string()
                                    ))
                            ));
                            if ui.button("detail").clicked() {
                                self.show_result.1.0 = true;
                                self.show_result.1.1 = TabSwitcher::MyCompiler;
                            }
                        }
                        ui.separator();
                        ui.label("gcc compile result");
                        if let Some(test_result) = &test_result.my_compiler {
                            egui::ScrollArea::vertical().show(ui, |ui| {
                                ui.label(format!(
                                    "exit code: {}",
                                    test_result.exit_code.map_or(
                                        "maybe time out".to_string(),
                                        |tmp| tmp.map_or(
                                            "process terminated by a signal".to_string(),
                                            |tmp| tmp.to_string()
                                        )
                                    )
                                ));
                            });
                            if ui.button("detail").clicked() {
                                self.show_result.1.0 = true;
                                self.show_result.1.1 = TabSwitcher::Gcc;
                            }
                        }
                    });
                }
            });
            if self.show_result.1.0 {
                egui::Window::new("stdio")
                    .open(&mut self.show_result.1.0)
                    .show(ctx, |ui| {
                        ui.horizontal(|ui| {
                            if ui
                                .selectable_label(
                                    self.show_result.2 == StdioSwitch::StdOut,
                                    "StdOut",
                                )
                                .clicked()
                            {
                                self.show_result.2 = StdioSwitch::StdOut;
                            }
                            if ui
                                .selectable_label(
                                    self.show_result.2 == StdioSwitch::StdErr,
                                    "StdErr",
                                )
                                .clicked()
                            {
                                self.show_result.2 = StdioSwitch::StdErr;
                            }
                        });
                        match self.show_result.1.1 {
                            TabSwitcher::MyCompiler => {
                                let test_result = self
                                    .tests_clone
                                    .get(0)
                                    .unwrap()
                                    .1
                                    .get(self.show_result.0.1)
                                    .unwrap()
                                    .may_be_result
                                    .clone()
                                    .unwrap()
                                    .my_compiler
                                    .unwrap();
                                if self.show_result.2 == StdioSwitch::StdOut {
                                    if let Some(test_result) = &test_result.stdout {
                                        egui::ScrollArea::vertical().show(ui, |ui| {
                                            ui.label(test_result);
                                        });
                                    }
                                } else {
                                    if let Some(test_result) = &test_result.stdout {
                                        egui::ScrollArea::vertical().show(ui, |ui| {
                                            ui.label(test_result);
                                        });
                                    }
                                }
                            }
                            TabSwitcher::Gcc => {
                                let test_result = self
                                    .tests_clone
                                    .get(0)
                                    .unwrap()
                                    .1
                                    .get(self.show_result.0.1)
                                    .unwrap()
                                    .may_be_result
                                    .clone()
                                    .unwrap()
                                    .gcc
                                    .unwrap();
                                if self.show_result.2 == StdioSwitch::StdOut {
                                    if let Some(test_result) = &test_result.stdout {
                                        egui::ScrollArea::vertical().show(ui, |ui| {
                                            ui.label(test_result);
                                        });
                                    }
                                } else {
                                    if let Some(test_result) = &test_result.stdout {
                                        egui::ScrollArea::vertical().show(ui, |ui| {
                                            ui.label(test_result);
                                        });
                                    }
                                }
                            }
                            _ => unreachable!(),
                        };
                    });
            }
        }
    }
}
