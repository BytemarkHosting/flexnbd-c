module FlexNBD
  def self.binary(str)
    if str.respond_to? :force_encoding
      str.force_encoding 'ASCII-8BIT'
    else
      str
    end
  end

  # eeevil is his one and only name...
  def self.read_constants
    parents = []
    current = File.expand_path('.')
    while current != '/'
      parents << current
      current = File.expand_path(File.join(current, '..'))
    end

    source_root = parents.find do |dirname|
      File.directory?(File.join(dirname, 'src'))
    end

    raise 'No source root!' unless source_root

    headers = Dir[File.join(source_root, 'src', '{common,proxy,server}', '*.h')]

    headers.each do |header_filename|
      txt_lines = File.readlines(header_filename)
      txt_lines.each do |line|
        if line =~ /^#\s*define\s+([A-Z0-9_]+)\s+(\d+)\s*$/
          # Bodge until I can figure out what to do with #ifdefs
          const_set(Regexp.last_match(1), Regexp.last_match(2).to_i) unless const_defined?(Regexp.last_match(1))
        end
      end
    end
  end

  read_constants

  REQUEST_MAGIC = binary("\x25\x60\x95\x13") unless defined?(REQUEST_MAGIC)
  REPLY_MAGIC = binary("\x67\x44\x66\x98") unless defined?(REPLY_MAGIC)
end # module FlexNBD
