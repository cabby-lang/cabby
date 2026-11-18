# Cabby's Homebrew formula. packaging/homebrew/build-tap.sh fills in the
# url, version and sha256, and puts it in a tap of its own (cabby/local).
class Cabby < Formula
  desc "Natively compiled, C-like programming language"
  homepage "@HOMEPAGE@"
  url "@URL@"
  version "@VERSION@"
  sha256 "@SHA256@"
  license "MIT"

  depends_on "pkgconf" => :build
  depends_on "libffi"
  depends_on "sdl3" => :recommended
  depends_on "ffmpeg" => :optional
  depends_on "gtk4" => :optional

  def install
    system "make", "PREFIX=#{prefix}"
    system "make", "install", "PREFIX=#{prefix}"
    (share/"vim/vimfiles").install Dir["editor/vim/*"]
  end

  def caveats
    <<~EOS
      Compiling (cabby -c) uses the clang of Apple's Command Line Tools:
        xcode-select --install
      For Vim, add this to ~/.vimrc:
        set runtimepath+=#{HOMEBREW_PREFIX}/share/vim/vimfiles
    EOS
  end

  test do
    (testpath/"hello.cby").write <<~EOS
      int main {
      \tprintln "hello,", 6 * 7
      \treturn 0
      }
    EOS
    assert_equal "hello, 42\n", shell_output("#{bin}/cabby hello.cby")
    system bin/"cabby", "-c", "hello.cby"
    assert_equal "hello, 42\n", shell_output("./hello")
  end
end
