DEBUG  = true

SOURCES = %w( flexnbd ioutil readwrite serve util )
OBJECTS = SOURCES.map { |s| "#{s}.o" }
LIBS    = %w( pthread )
CCFLAGS = %w( -Wall )
LDFLAGS = []

if DEBUG
  LDFLAGS << ["-g"]
  CCFLAGS << ["-g -DDEBUG"]
  LIBS    << ["efence"]
end

rule 'default' => 'flexnbd'

rule 'flexnbd' => OBJECTS do |t|
  sh "gcc #{LDFLAGS.join(' ')} "+
    LIBS.map { |l| "-l#{l}" }.join(" ")+
    " -o #{t.name} "+
    t.sources.join(" ")
end

rule '.o' => '.c' do |t|
  sh "gcc -c #{CCFLAGS.join(' ')} -o #{t.name} #{t.source} "
end

rule 'clean' do
  sh "rm -f flexnbd "+OBJECTS.join(" ")
end
