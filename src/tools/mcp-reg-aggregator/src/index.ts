import { S3Client, PutObjectCommand } from "@aws-sdk/client-s3";
import { mkdirSync, writeFileSync } from "fs";
import { join } from "path";

// ---------------------------------------------------------------------------
// Registry API types (subset of full OpenAPI schema)
// ---------------------------------------------------------------------------

interface KeyValueInput {
  name: string;
  description?: string;
  isRequired?: boolean;
  isSecret?: boolean;
  default?: string;
}

interface StdioTransport {
  type: "stdio";
}

interface HttpTransport {
  type: "streamable-http" | "sse";
  url: string;
  headers?: KeyValueInput[];
}

type LocalTransport = StdioTransport | HttpTransport;

interface ArgumentBase {
  type: "positional" | "named";
  description?: string;
  isRequired?: boolean;
  isRepeated?: boolean;
  default?: string;
  value?: string;
}

interface PositionalArgument extends ArgumentBase {
  type: "positional";
  valueHint?: string;
}

interface NamedArgument extends ArgumentBase {
  type: "named";
  name: string;
}

type Argument = PositionalArgument | NamedArgument;

interface Package {
  registryType: string;
  identifier: string;
  version?: string;
  runtimeHint?: string;
  runtimeArguments?: Argument[];
  packageArguments?: Argument[];
  transport: LocalTransport;
  environmentVariables?: KeyValueInput[];
}

interface RemoteTransport {
  type: "streamable-http" | "sse";
  url: string;
  headers?: KeyValueInput[];
}

interface Repository {
  url: string;
  source: string;
}

interface Icon {
  src: string;
  mimeType?: string;
  sizes?: string[];
  theme?: "light" | "dark";
}

interface ServerDetail {
  name: string;
  title?: string;
  description: string;
  version: string;
  repository?: Repository;
  websiteUrl?: string;
  icons?: Icon[];
  packages?: Package[];
  remotes?: RemoteTransport[];
}

interface OfficialMeta {
  status: "active" | "deprecated" | "deleted";
  statusMessage?: string;
  publishedAt?: string;
  updatedAt?: string;
  isLatest?: boolean;
}

interface ServerResponse {
  server: ServerDetail;
  _meta?: {
    "io.modelcontextprotocol.registry/official"?: OfficialMeta;
    [key: string]: unknown;
  };
}

interface ServerList {
  servers: ServerResponse[];
  metadata: {
    nextCursor?: string | null;
    count: number;
  };
}

// ---------------------------------------------------------------------------
// Condensed output types
// ---------------------------------------------------------------------------

interface CondensedKeyValueInput {
  name: string;
  description: string | null;
  required: boolean;
  secret: boolean;
  default: string | null;
}

interface CondensedArgument {
  type: "positional" | "named";
  name: string | null;
  value_hint: string | null;
  description: string | null;
  required: boolean;
  repeated: boolean;
  default: string | null;
  value: string | null;
}

interface CondensedPackage {
  registry_type: string;
  identifier: string;
  version: string | null;
  transport_type: "stdio" | "streamable-http" | "sse";
  headers: CondensedKeyValueInput[];
  runtime_hint: string | null;
  runtime_arguments: CondensedArgument[];
  package_arguments: CondensedArgument[];
  env_vars: CondensedKeyValueInput[];
}

interface CondensedRemote {
  type: "streamable-http" | "sse";
  url: string;
  headers: CondensedKeyValueInput[];
}

interface CondensedIcon {
  url: string;               // original icon URL
  data: string | null;       // base64-encoded bytes, SVG only
  mime_type: string | null;
  sizes: string[] | null;
  theme: "light" | "dark" | null;
}

interface CondensedServer {
  name: string;
  title: string | null;
  description: string;
  version: string;
  status: "active" | "deprecated" | "deleted";
  repository_url: string | null;
  website_url: string | null;
  icons: CondensedIcon[];
  packages: CondensedPackage[];
  remotes: CondensedRemote[];
}

interface McpRegistry {
  generated_at: string;
  count: number;
  servers: CondensedServer[];
}

// ---------------------------------------------------------------------------
// Fetch helpers
// ---------------------------------------------------------------------------

const REGISTRY_BASE = "https://registry.modelcontextprotocol.io";
const PAGE_LIMIT = 100;

async function fetchPage(cursor?: string): Promise<ServerList> {
  const params = new URLSearchParams({ limit: String(PAGE_LIMIT), version: "latest" });
  if (cursor) params.set("cursor", cursor);

  const url = `${REGISTRY_BASE}/v0.1/servers?${params}`;
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`Registry fetch failed: ${res.status} ${res.statusText} — ${url}`);
  }
  return res.json() as Promise<ServerList>;
}

async function fetchAllServers(outDir?: string): Promise<ServerResponse[]> {
  const all: ServerResponse[] = [];
  let cursor: string | undefined;
  let pageNum = 0;

  if (outDir) mkdirSync(outDir, { recursive: true });

  do {
    const page = await fetchPage(cursor);
    all.push(...page.servers);
    cursor = page.metadata.nextCursor ?? undefined;
    pageNum++;
    console.log(`Fetched ${all.length} servers so far (page count: ${page.metadata.count})`);

    if (outDir) {
      const pagePath = join(outDir, `page-${String(pageNum).padStart(3, "0")}.json`);
      writeFileSync(pagePath, JSON.stringify(page, null, 2), "utf-8");
      console.log(`  Saved ${pagePath}`);
    }
  } while (cursor);

  return all;
}

// ---------------------------------------------------------------------------
// Condensing logic
// ---------------------------------------------------------------------------

function condensedStatus(srv: ServerResponse): "active" | "deprecated" | "deleted" {
  return srv._meta?.["io.modelcontextprotocol.registry/official"]?.status ?? "active";
}

function condenseKeyValueInputs(inputs: KeyValueInput[]): CondensedKeyValueInput[] {
  return inputs.map((kv) => ({
    name: kv.name,
    description: kv.description ?? null,
    required: kv.isRequired ?? false,
    secret: kv.isSecret ?? false,
    default: kv.default ?? null,
  }));
}

function condensedPackage(pkg: Package): CondensedPackage {
  const transport = pkg.transport;
  const transportType: "stdio" | "streamable-http" | "sse" =
    transport.type === "stdio" ? "stdio"
    : transport.type === "streamable-http" ? "streamable-http"
    : "sse";

  const condenseArgs = (args: Argument[]): CondensedArgument[] => args
    .filter((arg) => arg.type === "positional" || arg.type === "named")
    .map((arg) => ({
      type: arg.type,
      name: arg.type === "named" ? arg.name : null,
      value_hint: arg.type === "positional" ? (arg.valueHint ?? null) : null,
      description: arg.description ?? null,
      required: arg.isRequired ?? false,
      repeated: arg.isRepeated ?? false,
      default: arg.default ?? null,
      value: arg.value ?? null,
    }));

  return {
    registry_type: pkg.registryType,
    identifier: pkg.identifier,
    version: pkg.version ?? null,
    transport_type: transportType,
    headers: transport.type !== "stdio" ? condenseKeyValueInputs(transport.headers ?? []) : [],
    runtime_hint: pkg.runtimeHint ?? null,
    runtime_arguments: condenseArgs(pkg.runtimeArguments ?? []),
    package_arguments: condenseArgs(pkg.packageArguments ?? []),
    env_vars: condenseKeyValueInputs(pkg.environmentVariables ?? []),
  };
}

async function fetchIconData(icon: Icon): Promise<CondensedIcon> {
  const base: CondensedIcon = {
    url: icon.src,
    data: null,
    mime_type: icon.mimeType ?? null,
    sizes: icon.sizes ?? null,
    theme: icon.theme ?? null,
  };

  if (icon.mimeType !== "image/svg+xml") return base;

  try {
    const res = await fetch(icon.src);
    if (!res.ok) {
      console.warn(`Icon fetch failed (${res.status}): ${icon.src}`);
      return base;
    }
    const buffer = await res.arrayBuffer();
    return { ...base, data: Buffer.from(buffer).toString("base64") };
  } catch (err) {
    console.warn(`Icon fetch error: ${icon.src} — ${(err as Error).message}`);
    return base;
  }
}

async function condenseServer(srv: ServerResponse): Promise<CondensedServer | null> {
  const { server } = srv;

  // Only keep packages with stdio transport
  const stdioPackages = (server.packages ?? []).filter((pkg) => pkg.transport.type === "stdio");
  const remotes = (server.remotes ?? []).map((r) => ({
    type: r.type,
    url: r.url,
    headers: condenseKeyValueInputs(r.headers ?? []),
  }));

  // Skip servers with no stdio packages and no remotes
  if (stdioPackages.length === 0 && remotes.length === 0) return null;

  const icons = await Promise.all((server.icons ?? []).map(fetchIconData));

  return {
    name: server.name,
    title: server.title ?? null,
    description: server.description,
    version: server.version,
    status: condensedStatus(srv),
    repository_url: server.repository?.url ?? null,
    website_url: server.websiteUrl ?? null,
    icons,
    packages: stdioPackages.map(condensedPackage),
    remotes,
  };
}

// ---------------------------------------------------------------------------
// S3 upload
// ---------------------------------------------------------------------------

async function uploadToS3(payload: string): Promise<void> {
  const bucket = process.env.S3_BUCKET;
  const region = process.env.AWS_REGION ?? "us-east-1";
  const key = process.env.S3_KEY ?? "mcp/registry.json";

  if (!bucket) throw new Error("S3_BUCKET environment variable is required");

  const client = new S3Client({ region });
  await client.send(
    new PutObjectCommand({
      Bucket: bucket,
      Key: key,
      Body: payload,
      ContentType: "application/json",
    })
  );
  console.log(`Uploaded ${key} to s3://${bucket}/${key}`);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
  const outputFlag = process.argv.indexOf("--output");
  const outputFile = outputFlag !== -1 ? process.argv[outputFlag + 1] : undefined;
  if (outputFlag !== -1 && !outputFile) throw new Error("--output requires a file path argument");

  // When writing to a file, store raw pages alongside it
  const outDir = outputFile ? join(outputFile, "..", "pages") : undefined;

  console.log("Fetching MCP registry…");
  const raw = await fetchAllServers(outDir);

  // Drop deleted servers; keep active + deprecated
  const filtered = raw.filter((s) => condensedStatus(s) !== "deleted");
  console.log(`Total: ${raw.length}, after filtering deleted: ${filtered.length}`);

  // Condense servers with bounded concurrency to avoid exhausting sockets during icon fetches
  const CONCURRENCY = 20;
  const condensed: CondensedServer[] = [];
  for (let i = 0; i < filtered.length; i += CONCURRENCY) {
    const batch = filtered.slice(i, i + CONCURRENCY);
    const results = await Promise.all(batch.map(condenseServer));
    condensed.push(...results.filter((s): s is CondensedServer => s !== null));
    if ((i + CONCURRENCY) % 200 === 0) {
      console.log(`Condensed ${condensed.length} / ${filtered.length}`);
    }
  }
  const output: McpRegistry = {
    generated_at: new Date().toISOString(),
    count: condensed.length,
    servers: condensed,
  };

  const json = JSON.stringify(output, (key, value) => {
    if (value === null) return undefined;
    if (Array.isArray(value) && value.length === 0 && key !== "servers") return undefined;
    return value;
  }, 2);

  if (outputFile) {
    writeFileSync(outputFile, json, "utf-8");
    console.log(`Written to ${outputFile}`);
  } else {
    await uploadToS3(json);
  }

  console.log("Done.");
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
