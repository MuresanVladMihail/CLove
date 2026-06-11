![Alt text](tools/logo.png "FH")

# FH

FH is designed to be embedded inside native applications (e.g. game engines),
where scripts are sandboxed, deterministic, and tightly integrated with C code.

Test with:

```text
$ make -j2
$ ./fh tests/mandelbrot.fh
```

## Features

- compilation to bytecode
- register-based VM.
- simple mark-and-sweep garbage collector
- full closures
- dynamic typing with `null`, `boolean`, `number`, `string`, `array`,
  `map`, `closure` `c_func` and `c user defined objects`
- simple standard library
- simple regex support
- simple tar support
- own packaging format
- hashing algoriths: bcrypt and md5
- uses fast and uniform random generator, mt19937
- just ~10k C loc

## Implementation Notes

- register-based virtual machine (fast dispatch)
- bytecode compiler
- compact (~10k lines of C)
- no external runtime dependencies

## Example Code

### Closures

```
fn make_counter(num) {
    return {
        "next" : fn() {
            num = num + 1;
        },

        "read" : fn() {
            return num;
        },
    };
}

fn main() {
    let c1 = make_counter(0);
    let c2 = make_counter(10);
    c1.next();
    c2.next();
    printf("%d, %d\n", c1.read(), c2.read());    # prints 1, 11

    c1.next();
    if (c1.read() == 2 && c2.read() == 11) {
        printf("ok!\n");
    } else {
        error("this should will not happen");
    }
}
```

### Mandelbrot Set

```
# check point c = (cx, cy) in the complex plane
fn calc_point(cx, cy, max_iter)
{
    # start at the critical point z = (x, y) = 0
    let x = 0;
    let y = 0;

    let i = 0;
    while (i < max_iter) {
        # calculate next iteration: z = z^2 + c
        let t = x*x - y*y + cx;
        y = 2*x*y + cy;
        x = t;

        # stop if |z| > 2
        if (x*x + y*y > 4)
            break;
        i = i + 1;
    }
    return i;
}

fn mandelbrot(x1, y1, x2, y2, size_x, size_y, max_iter)
{
    let step_x = (x2-x1) / (size_x-1);
    let step_y = (y2-y1) / (size_y-1);

    let y = y1;
    while (y <= y2) {
        let x = x1;
        while (x <= x2) {
            let n = calc_point(x, y, max_iter);
            if (n == max_iter)
                printf(".");         # in Mandelbrot set
            else
                printf("%d", n%10);  # outside
            x = x + step_x;
        }
        y = y + step_y;
        printf("\n");
    }
}

fn main()
{
  mandelbrot(-2, -2, 2, 2, 150, 50, 1500);
}
```

### Fibonacci

```
fn fib(n)
{
    if (n >= 2) {
        return fib (n - 1) + fib (n - 2);
    }
    else {
        return n;
    }
}

fn main() {
    printf("%f\n", fib(35));
}
```

## License

Copyright (c) 2019-2025 Mureşan Vlad Mihail

Contact Info muresanvladmihail@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Contribuitors

- Ricardo Massaro
- Bitpuffin
