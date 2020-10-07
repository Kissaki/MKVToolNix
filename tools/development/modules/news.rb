def parse_news file_name
  news         = []
  current_line = []
  version      = 'HEAD'
  date         = 'unknown'
  type         = 'Other changes'
  ignore       = true

  flush        = lambda do
    current_line = current_line.join("\n").gsub(%r{\A\n+|\n+\z}, '')

    if !current_line.empty?
      news << {
        :content => current_line,
        :version => version,
        :date    => date,
        :type    => type
      }
    end

    current_line = []
  end

  IO.read(file_name).
    gsub(%r{<!--.*?-->}, '').
    split(%r{\n}).
    each do |line|

    flush.call if %r{^[#*]}.match(line)

    #  # Version 9.7.1 "Pandemonium" 2016-12-27
    #  # Version 0.6.4 2003-08-27

    if match = %r{ ^\# \s+ Version \s+ (?<version> [0-9.-]+) (?: \s+ " (?<codename>.+) " )? \s+ (?<year>\d+)-(?<month>\d+)-(?<day>\d+) }x.match(line)
      version = match[:version]
      date    = sprintf("%04d-%02d-%02d", match[:year].to_i, match[:month].to_i, match[:day].to_i)
      type    = 'Other changes'
      ignore  = false

    elsif match = %r{ ^\# \s+ Version \s+ \? }x.match(line)
      ignore = true

    elsif ignore
      next

    elsif %r{ ^\#\# \s+ (.+) }x.match(line)
      type = $1

    else
      flush.call if /^\*/.match(line)
      current_line << line.gsub(%r{^\*\s*}, '').sub(%r{^\s\s}, '')

    end
  end

  flush.call

  news.chunk { |e| e[:version] }
end
