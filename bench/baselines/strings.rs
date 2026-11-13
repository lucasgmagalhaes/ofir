// Building and splitting a string: the Rust baseline of programs/strings.of.
use std::fmt::Write;

fn main() {
    let n: i64 = 1000000;
    let mut text = String::new();
    for i in 0..n {
        write!(text, "{},", i * 7).unwrap();
    }
    let mut parts: i64 = 0;
    let mut digits: i64 = 0;
    for part in text.split(',') {
        parts += 1;
        digits += part.len() as i64;
    }
    println!("{} {} {}", text.len(), parts, digits);
}
