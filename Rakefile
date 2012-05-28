DEBUG  = true

SOURCES = %w( flexnbd ioutil readwrite serve util parse control remote )
OBJECTS = SOURCES.map { |s| "#{s}.o" }
LIBS    = %w( pthread )
CCFLAGS = %w( -Wall )
LDFLAGS = []
LIBCHECK = "/usr/lib/libcheck.a"

TEST_MODULES = Dir["tests/check_*.c"].map { |n| n[12..-3] }

if DEBUG
  LDFLAGS << ["-g"]
  CCFLAGS << ["-g -DDEBUG"]
end

desc "Build flexnbd binary"
rule 'default' => 'flexnbd'

namespace "test" do
  desc "Run all tests"
  task 'run' => ["unit", "scenarios"]
  
  desc "Build C tests"
  task 'build' => TEST_MODULES.map { |n| "tests/check_#{n}" }
  
  desc "Run C tests"
  task 'unit' => 'build' do
    TEST_MODULES.each do |n|
      ENV['EF_DISABLE_BANNER'] = '1'
      sh "./tests/check_#{n}"
    end
  end
  
  desc "Run NBD test scenarios"
  task 'scenarios' => 'flexnbd' do
    sh "cd tests; ruby nbd_scenarios"
  end
end

def gcc_link(target, objects)
  sh "gcc #{LDFLAGS.join(' ')} "+
    LIBS.map { |l| "-l#{l}" }.join(" ")+
    " -o #{target} "+
    objects.join(" ")
end

rule 'flexnbd' => OBJECTS do |t|
  gcc_link(t.name, t.sources)
end

rule(/tests\/check_[a-z]+$/ => [ proc { |target| [target+".o", "util.o"] } ]) do |t|
  gcc_link(t.name, t.sources + [LIBCHECK])
end

rule '.o' => '.c' do |t|
  sh "gcc -I. -c #{CCFLAGS.join(' ')} -o #{t.name} #{t.source} "
end

desc "Remove all build targets, binaries and temporary files"
rule 'clean' do
  sh "rm -f *~ flexnbd " + (
    OBJECTS + 
    TEST_MODULES.map { |n| ["tests/check_#{n}", "tests/check_#{n}.o"] }.flatten
  ).
  join(" ")
end
