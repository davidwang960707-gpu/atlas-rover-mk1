# 机器人积木化设计器平台 API 契约

版本：`robot-brick-platform-api.v1`

本文档描述当前本地 Platform API 原型已经实现的正式后端契约。实现文件为 `tools/platform_api_server.mjs`，数据默认写入 `var/platform_api/`。该实现仍是本地文件存储，但资源边界按未来 Postgres、对象存储和任务队列设计。

## 1. 运行方式

```bash
node tools/platform_api_server.mjs --port=8787
```

健康检查：

```http
GET /api/health
GET /api/platform/contract
```

本地身份通过 `x-user-id` 请求头模拟；未传时使用 `user-local`。

## 2. 资源模型

当前集合模拟未来数据库表：

| 资源 | 用途 |
| --- | --- |
| `users` | 用户身份，当前用请求头模拟 |
| `workspaces` | 个人/团队工作区 |
| `workspace_members` | 工作区成员和角色 |
| `projects` | 机器人设计项目元数据 |
| `project_branches` | 项目分支 |
| `project_versions` | 命名版本和文档快照 |
| `design_documents` | 当前可编辑设计文档 |
| `design_document_revisions` | 设计文档修订历史和审计摘要 |
| `assets` | 上传资产元数据 |
| `asset_uploads` | 上传会话 |
| `asset_jobs` | 资产处理队列 |
| `asset_derivatives` | 资产派生物，如缩略图、网格预览和元数据 |
| `asset_replacements` | 资产替代候选关系，用于替换前影响分析和复用建议 |
| `storage_objects` | 对象存储记录，当前为本地文件适配层，未来可替换 S3/R2/MinIO |
| `component_packages` | 组件包元数据 |
| `component_package_versions` | 组件包版本内容 |
| `package_publication_requests` | 组件包公开发布审核请求、阻断报告和审核结果 |
| `export_jobs` | 制造输出归档任务，如 BOM、SVG、制造包 |
| `export_artifacts` | 制造输出内容归档和校验信息 |
| `share_links` | 只读/评论分享链接 |
| `share_comments` | 评论分享链接下的评审意见 |
| `audit_events` | 关键操作审计 |

## 3. 用户空间

```http
GET  /api/users/me
GET  /api/workspaces
POST /api/workspaces
GET    /api/workspaces/:workspaceId/members
POST   /api/workspaces/:workspaceId/members
PATCH  /api/workspaces/:workspaceId/members/:userId
DELETE /api/workspaces/:workspaceId/members/:userId
```

`GET /api/users/me` 会返回当前用户和默认工作区。`POST /api/workspaces` 创建团队或个人空间。

工作区成员角色：

- `owner`：管理工作区成员、项目、资产、组件包和分享链接。
- `editor`：创建和编辑项目、资产、组件包，可创建分享链接。
- `viewer`：只读访问工作区内可见资源。

成员写操作要求当前用户是 `owner`。本地默认个人工作区会把创建者/当前模拟用户保持为 `owner`，方便开源本地运行时直接管理成员；其他模拟用户进入默认空间时仍按受邀角色或 `editor` 兼容角色处理。正式 SaaS 环境应改成明确邀请或注册后的个人工作区初始化。

前端“工作区与权限”页面已接入这些成员接口：可查看成员列表、邀请/更新成员、切换 `owner/editor/viewer` 角色并移除成员。项目详情里的“邀请编辑者”保留为快捷入口，但成员治理的主入口是工作区页面。

## 4. 项目与版本

```http
GET    /api/projects?workspaceId=&page=&pageSize=&q=&visibility=&robotPackage=&owner=
POST   /api/projects
GET    /api/projects/:projectId
PATCH  /api/projects/:projectId
DELETE /api/projects/:projectId

GET /api/projects/:projectId/document?branchId=
PUT /api/projects/:projectId/document?branchId=
GET /api/projects/:projectId/document/history?branchId=&page=&pageSize=

GET  /api/projects/:projectId/versions
POST /api/projects/:projectId/versions
POST /api/projects/:projectId/branches
GET  /api/projects/:projectId/diff?from=&to=
```

项目创建时会自动生成：

- `main` 分支。
- 初始 `design_document`。
- 初始 `project_version`。

`GET /api/projects` 返回：

- `projects`：当前页项目。
- `pagination`：`page/pageSize/total/totalPages/hasPrev/hasNext`。
- `filters`：服务端实际使用的筛选条件。

`PUT /api/projects/:projectId/document` 支持 `expectedRevision` 或 `If-Match`。服务端会对 `design_documents.revision` 做乐观锁校验，冲突时返回 `409 document_conflict`，并附带 `currentRevision`、`expectedRevision` 和当前云端文档。

每次项目创建、文档保存、分支创建都会写入 `design_document_revisions`。`history` 接口返回最近修订、页码和总数，用于前端右侧评审面板显示“谁在什么时候保存/创建了什么”。

`versions` 保存命名快照，`branches` 从当前文档或指定版本创建新分支，`diff` 返回组件增删和姿态变化摘要。项目读写会按资源 `visibility`、当前用户和工作区成员角色过滤；写入要求 `owner` 或 `editor`。

## 5. 资产上传与处理队列

兼容前端直传：

```http
POST /api/assets
GET  /api/assets?workspaceId=&page=&pageSize=&q=&kind=&status=&visibility=
GET  /api/assets/:assetId
GET  /api/assets/:assetId/content
GET  /api/assets/:assetId/derivatives/:derivativeId/content
GET  /api/assets/:assetId/download-url
GET  /api/assets/:assetId/references
GET  /api/assets/:assetId/replacements
POST /api/assets/:assetId/replacements
GET  /api/assets/:assetId/versions
POST /api/assets/:assetId/versions
GET  /api/assets/:assetId/versions/:versionId
GET  /api/assets/:assetId/versions/:versionId/content
GET  /api/assets/:assetId/versions/:versionId/download-url
POST /api/assets/:assetId/versions/:versionId/restore
POST /api/assets/reprocess
POST /api/assets/bulk-update
GET  /api/assets/governance?workspaceId=&q=
PATCH /api/assets/:assetId
DELETE /api/assets/:assetId
GET  /api/storage/objects/:objectId
GET  /api/storage/objects/:objectId/content
GET  /api/storage/objects/:objectId/signed-url?action=read&expiresIn=900
GET  /api/storage/signed/:token
```

正式上传会话：

```http
POST /api/assets/uploads
POST /api/assets/:assetId/complete
GET  /api/asset-jobs?status=&assetId=&page=&pageSize=
POST /api/asset-jobs/run-next
GET  /api/asset-jobs/:jobId
PATCH /api/asset-jobs/:jobId
```

上传完成后生成 `asset_jobs`，本地 worker 通过 `run-next` 每次处理一个排队任务并写入 `asset_derivatives`。当前已实现本地对象存储适配层：源文件、版本快照和派生物都会写入 `storage_objects`，API 暴露下载 URL，但不会泄露本地文件路径。资产源文件、派生物、版本快照和存储对象的 `contentUrl`/`uri` 会在响应时按当前请求 origin 动态生成，历史数据里保存过的旧端口或旧域名不会直接透传给前端。

对象存储已新增签名读取契约：已授权用户可从 `storage_objects/:objectId/signed-url` 获取短时 `read/download` URL，本地实现使用 HMAC token 和 `/api/storage/signed/:token` 代理读取；正式部署时可替换为 S3/R2/OSS presigned URL。

当前 worker 已支持：

- STL ASCII/Binary：解析三角面、顶点数、包围盒和尺寸。
- GLB：读取 GLB header、版本、JSON chunk 中的 mesh/node/material 数。
- STEP/STP：读取 schema、实体数量和毫米单位提示。
- 派生物：`metadata` JSON、`thumbnail` SVG、`mesh-preview` JSON。

环境变量：

- `ROBOT_PLATFORM_STORAGE_PROVIDER`：默认 `local-fs`，未来可切到 S3 compatible provider。
- `ROBOT_PLATFORM_STORAGE_BUCKET`：默认 `robot-platform-local`。
- `ROBOT_PLATFORM_OBJECT_ROOT`：默认 `var/platform_api/objects`。

资产处理状态机：

- `uploading`：已创建上传会话，等待文件完成。
- `uploaded`：文件已完成，等待处理。
- `processing`：至少一个处理任务排队或运行中。
- `ready`：全部处理任务完成，组件和主画布可直接引用。
- `needs_review`：至少一个处理任务失败，需要用户检查或重新处理。

`asset_jobs` 会记录 `queued/running/completed/failed/cancelled`、worker、开始/完成/失败时间、耗时和错误信息。`run-next` 支持返回本次处理的单个 job，便于前端资产队列做实时进度刷新。`POST /api/assets/reprocess` 可批量把指定资产的 `metadata/thumbnail/mesh-preview` 任务重新排队，用于资产解析逻辑升级或用户手动修复。`PATCH /api/asset-jobs/:jobId` 支持 `{ "action": "retry" }` 和 `{ "action": "cancel" }`，前端资产队列可以对失败或卡住的任务做单项处理。

资产列表同样返回 `assets/pagination/filters`，前端组件库资产队列面板按页读取资产元数据，并保留 `asset_jobs` 作为处理进度来源。处理完成后的资产会带上 `storageProvider`、`storageObjectId`、`modelFormat`、`dimensions`、`triangleCount`、`derivativeCount` 和 `derivativeSummary`，组件库可直接展示“是否已可用、尺寸是否可信、是否已有缩略图/网格预览”。

`GET /api/assets/:assetId` 会同时返回：

- `references`：扫描组件包版本和设计文档，返回该资产影响到的组件定义和项目实例。
- `replacements`：返回该资产已登记的替代候选和反向替代关系。
- `derivatives`：派生物列表，含 `contentUrl`。
- `versions`：源文件版本快照列表，含 `versionNumber`、label、checksum、大小和下载入口。

`PATCH /api/assets/:assetId` 当前支持更新 `name/fileName`、`visibility`、`metadata`、`license`、`licenseUrl`、`sourceUrl`，也支持归档/恢复状态。`DELETE /api/assets/:assetId` 是软归档，不会删除对象存储文件；`POST /api/assets/bulk-update` 支持批量设置授权字段、恢复或归档。前端资产库使用这些字段管理模型来源、授权可用性和平台化复用边界。

资产版本支持详情读取、版本内容下载和恢复。`POST /api/assets/:assetId/versions/:versionId/restore` 会先为当前源文件创建安全快照，再把资产源对象切回目标版本，并重新排队生成 `metadata`、`thumbnail` 和 `mesh-preview`，用于模型上传错误、CAD 导出错误或用户误覆盖后的快速回滚。

`GET /api/assets/governance` 返回资产治理汇总和问题桶，包含 `missingLicense`、`unused`、`needsReview`、`processing`、`publicWithoutLicense`。每个桶会返回前 50 条资产摘要，供资产库页面做批量补许可证、批量归档未引用资产、重新处理失败/待处理资产等跨页治理动作。

组件定义可以只保存 `assetId`，不必复制 `uri`。前端会优先通过平台资产详情补齐 `contentUrl` 再加载真实 STL/GLB。这样组件包发布、fork 和项目复用时，文件归属仍留在资产库。

## 6. 组件包 Registry

```http
GET    /api/packages/catalog
GET    /api/packages?scope=official|public|workspace|all&workspaceId=&page=&pageSize=&q=&visibility=&sourceKind=
POST   /api/packages
GET    /api/packages/:packageId
DELETE /api/packages/:packageId

POST /api/packages/:packageId/versions
POST /api/packages/:packageId/validate
POST /api/packages/:packageId/publish
POST /api/packages/:packageId/publish-request
GET  /api/package-publication-requests?workspaceId=&packageId=&status=&q=&page=&pageSize=
GET  /api/package-publication-requests/:requestId
PATCH /api/package-publication-requests/:requestId
POST /api/packages/:packageId/fork
```

`catalog` 会合并官方静态包和已公开发布的本地组件包。私有包默认只属于 workspace，必须通过 `/api/packages?scope=workspace&workspaceId=...` 读取，避免私有组件包进入公共目录。`publish` 通过基础校验和资产依赖检查后变为公开包，`fork` 可从官方包或用户包复制成私有包。

`validate`、`versions` 和 `publish` 会返回 `assetDependencies`。发布时如果组件包引用的 `assetId` 缺失、已归档或未处理到 `ready`，后端会以 `422 package_asset_dependency_failed` 阻止发布；许可证缺失当前作为 warning 返回，便于开源组件包发布前补齐来源和授权信息。

公开生态推荐走审核流：`publish-request` 会读取最新组件包版本，同时写入结构校验和资产依赖报告。校验或资产阻断时请求状态为 `blocked`，否则为 `pending_review`。`GET /api/package-publication-requests/:requestId` 返回审核请求、组件包、当前版本摘要、与上一版本的组件/资产差异、依赖报告、校验报告和审计事件；`owner` 可通过 `PATCH /api/package-publication-requests/:requestId` 提交 `{ "action": "approve" }` 或 `{ "action": "reject", "note": "..." }`；批准后组件包可见性变为 `public`，驳回会保留审核意见。列表接口返回 `requests/pagination/filters`，支持按工作区、组件包、状态和关键词筛选。

组件包列表返回 `packages/pagination/filters`。`scope=workspace` 用于工作区私有包管理，`scope=public/official/all` 用于公共组件市场、官方组件包和全局可见包浏览；前端可在市场卡片上直接查看详情或 fork 为工作区私有包。

## 7. 制造输出 ExportJob

```http
GET  /api/projects/:projectId/exports?page=&pageSize=&kind=&status=
POST /api/projects/:projectId/exports
GET  /api/exports/:exportId
GET  /api/exports/:exportId/download-url
GET  /api/exports/:exportId/content
```

当前本地实现是同步归档：前端提交 BOM、装配步骤、检查清单、底板 SVG、孔位图、线束清单或制造包 HTML 后，服务端立即创建 `completed` 状态的 `export_job`，并把内容写入 `export_artifacts`。未来接入正式队列和对象存储时，可以把同一接口改成 `queued/running/completed/failed` 的异步 worker。

支持的 `kind`：

- `bom_csv`
- `assembly_steps_md`
- `checklist_md`
- `layout_json`
- `baseplate_svg`
- `holemap_svg`
- `harness_csv`
- `manufacturing_pack_html`

`export_job` 会保存项目、工作区、分支、项目版本、设计文档 revision、文件名、mime、大小、SHA-256、摘要、创建者、完成时间和下载 URL。读写权限跟随项目：读取要求能读项目，创建要求工作区 `owner` 或 `editor`。

## 8. 分享与权限

```http
GET    /api/share-links?targetType=&targetId=&permission=&includeRevoked=true
POST   /api/share-links
DELETE /api/share-links/:shareId
GET    /api/shares/:token
GET    /api/shares/:token/comments
POST   /api/shares/:token/comments
```

当前支持分享目标：

- `project`
- `package`
- `version`

`permission` 支持 `read` 和 `comment`。分享解析会返回 `capabilities`，当前统一为只读查看：`read=true`、`write=false`、`download=true`、`fork=true`，评论链接额外返回 `comment=true`。

`comment` 链接可写入 `share_comments`，`read` 链接只能读取目标资源，不能提交评论。项目、组件包、资产均有 `visibility` 字段，当前支持 `private`、`workspace`、`public`、`unlisted`。本地 API 会按当前用户、工作区成员和资源可见性过滤读取结果。

分享列表会返回 `url`、`targetLabel`、`capabilities`、`commentCount` 和 `revokedAt`。前端“分享中心”使用 `includeRevoked=true` 展示有效/已撤销链接，支持按目标类型、权限和关键词筛选，能从项目、组件包或项目版本创建只读/评论链接，并支持复制和撤销。

生产环境应保存 token hash；本地原型为了便于调试保存明文 token。

## 9. 平台洞察

```http
GET /api/platform/impact?workspaceId=&q=
GET /api/platform/readiness?workspaceId=
GET /api/platform/backend-contract?workspaceId=
```

影响图接口把当前用户可见的工作区、项目、组件包、资产、发布审核和分享链接整理为 `nodes/edges/risks/summary`。典型边包括项目使用组件包、组件包引用资产、项目实例引用资产、组件包发起发布审核、资源被分享为只读/评论链接。`risks` 会标记缺许可证资产、待复核/处理中资产、被阻断或待审核的组件包发布请求。前端“平台影响图”页面用它做全局引用关系和运营风险入口。

准备度接口返回 `dimensions/totals/migrationTasks`，按用户空间与权限、项目版本、对象存储、资产 worker、组件包发布、分享权限、制造输出、审计观测等维度描述从本地 API 原型迁移到正式在线后端的状态。前端洞察页用它展示正式后端准备度，后续迁移数据库、对象存储签名 URL、队列 worker 和正式鉴权时可作为检查清单。

后端契约接口返回 `database/objectStorage/queues/auth/migrationGates`：数据库部分定义 Postgres 表、索引、外键、JSONB 字段和迁移阶段；对象存储部分定义 provider、bucket、前缀和签名 URL 策略；队列部分定义资产处理、制造导出、组件包发布审核 worker；权限部分定义 workspace 角色、分享能力和资源策略。

## 10. 兼容接口

现有前端 API mode 仍使用以下兼容接口：

```http
GET/PUT /api/workspaces/default
GET/PUT /api/documents?key=...
POST    /api/assets
GET     /api/packages/catalog
GET     /api/packages/:packageId
```

这些接口会继续保留，直到前端项目管理和组件包管理完全迁移到正式资源模型。

## 11. 前端接入状态

当前 `simulator_web/rover_builder` 的 `platform=api` 模式已经接入部分正式资源模型：

| 页面 | 已接入能力 |
| --- | --- |
| 项目管理 | 从 `/api/projects` 分页/搜索/筛选加载项目列表和当前设计文档；新建、复制、重命名、删除项目写正式 Project API；保存云端命名版本；创建只读/评论分享链接；切换公开/私有；显示 revision 和冲突操作 |
| 设计器 | 本地草稿保存后进入云端设计文档保存队列；支持保存中、已保存、失败、冲突；冲突后可拉取云端或覆盖云端 |
| 项目详情侧栏 | 显示当前工作区角色、成员数量、文档历史、制造输出历史；支持刷新历史、刷新制造输出和邀请编辑者 |
| 平台洞察 | 从 `/api/platform/impact` 生成项目、组件包、资产、审核和分享的全局影响图；支持搜索、节点选择、风险队列和跳转到对应资源 |
| 分享中心 | 从 `/api/share-links` 读取 project/package/version 分享链接；支持只读/评论链接创建、复制、撤销、已撤销链接查看和评论数展示 |
| 组件包管理 | 从 `/api/packages?scope=workspace/public/official/all` 分页/搜索/筛选加载工作区私有包和公开组件市场；用户包编辑自动保存到 Registry；支持提交发布审核、刷新审核状态、查看审核详情与版本差异、批准/驳回审核、发布前资产依赖阻断提示；保留直接发布管理员捷径；Fork 官方/公开包为工作区私有包；显示 Registry ID/可见性/同步时间；组件包页已增加生态后台概览和公开市场卡片，可集中处理审核队列 |
| 组件库 | 显示平台资产队列，按 `/api/assets` 分页读取资产，刷新资产/任务状态，触发 `run-next` 处理任务 |
| 资产库 | 独立页面按正式 Asset API 分页/搜索/筛选；支持上传模型、真实模型/派生缩略预览、查看派生物/引用影响/替代件、维护许可证和来源链接、创建版本快照、批量重处理、批量授权和归档资产；资产治理面板接入 `/api/assets/governance`，可发现缺许可证、未引用、需复核、处理中和公开未授权资产，支持治理桶明细、单项打开/选择和批量动作 |
| 制造预览 | 保留浏览器即时导出，同时在 `platform=api` 模式下可把制造包和 BOM 归档到 `/api/projects/:projectId/exports` 并显示最近归档 |

前端仍保留 `GET/PUT /api/workspaces/default` 和 `/api/documents?key=...` 作为旧数据回退、本地草稿和兼容接口；`platform=api` 正常路径已经不再依赖该大 JSON 承载项目列表和私有组件包列表。

## 12. 验证

```bash
node --check tools/platform_api_server.mjs
node tools/validate_platform_api_contract.mjs
```

验证覆盖：

- 用户空间。
- 工作区成员、角色权限和非成员写入拒绝。
- 项目列表分页/搜索/筛选、项目创建、revision 文档保存、冲突返回、文档历史、命名版本、分支和 diff。
- 制造输出归档、列表筛选、详情、下载 URL、内容下载和非成员写入拒绝。
- 资产列表分页/筛选、上传会话、单任务处理队列、派生物、运行时 URL 重写、失败后的 `needs_review` 状态和资产治理汇总。
- 私有/公开/官方组件包列表分页/搜索/筛选、版本、发布、发布审核详情和版本差异、资产依赖阻断、公开市场和 fork。
- 对象存储签名 URL。
- 平台影响图的节点、边、风险队列、可视化筛选、正式后端准备度和后端契约。
- 只读/评论分享链接解析、分享能力声明、评论写入权限、列表字段和撤销后不可访问。
