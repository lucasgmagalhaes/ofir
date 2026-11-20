# 2. Basics

## Values, functions and control flow

<!-- check: run -->
```ofir
fn square(n: int) int {
    return n * n
}

fn main() {
    let total = 0
    for i in 1..4 {
        total += square(i)
    }
    if total > 10 {
        println("big: {total}")
    } else {
        println("small: {total}")
    }
}
```

```text
big: 14
```

A function names the type of each parameter and, after the parameter
list, the type it returns. `let` declares a variable that can be
assigned again; `const` declares one that cannot. `1..4` is the range
1, 2, 3: its end is excluded. `if` needs no parentheses, and its
branches are blocks.

## Structs, enums and `match`

<!-- check: run -->
```ofir
struct Point {
    x: int
    y: int

    fn manhattan(self) int {
        return self.x.abs() + self.y.abs()
    }
}

enum Direction {
    North
    East
    South
    West
}

fn step(p: Point, d: Direction) Point {
    return match d {
        Direction.North => Point{x: p.x, y: p.y + 1}
        Direction.East => Point{x: p.x + 1, y: p.y}
        Direction.South => Point{x: p.x, y: p.y - 1}
        Direction.West => Point{x: p.x - 1, y: p.y}
    }
}

fn main() {
    let p = Point{x: 0, y: 0}
    p = step(p, Direction.North)
    p = step(p, Direction.West)
    p = step(p, Direction.West)
    println("({p.x}, {p.y}) is {p.manhattan()} steps away")
}
```

```text
(-2, 1) is 3 steps away
```

A struct declares its fields and its methods together. A method whose
first parameter is `self` is called on a value, `p.manhattan()`. An
enum lists its variants, and a variant is always named through its
enum: `Direction.North`. `match` must cover every variant; leaving one
out is an error (E0500), so adding a variant later shows every `match`
to update.

Variants can carry values, and `match` takes them apart:

<!-- check: run -->
```ofir
enum Shape {
    Circle(radius: float)
    Rect(width: float, height: float)
}

fn area(s: Shape) float {
    return match s {
        Shape.Circle(r) => 3.0 * r * r
        Shape.Rect(w, h) => w * h
    }
}

fn main() {
    const shapes: List<Shape> = [Shape.Circle(1.0), Shape.Rect(2.0, 3.0)]
    for s in shapes {
        println("{area(s)}")
    }
}
```

```text
3.0
6.0
```

`List<Shape>` is a growable list of shapes, written with brackets.
`println` prints a string, so the area goes inside one.
